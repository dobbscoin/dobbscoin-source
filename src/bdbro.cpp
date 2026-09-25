// Copyright (c) 2024 The Bitcoin Core developers
// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Read-only Berkeley DB btree parser. The page layouts and the checks below
// follow Bitcoin Core v28.0 src/wallet/migrate.cpp (BerkeleyRODatabase, by
// Ava Chow and the Bitcoin Core developers, MIT licensed). Differences from
// that file: no stream classes (the whole file is read into memory and decoded
// with bounds-checked helpers), every page's LSN is checked including the last
// one, a key/data pair is skipped when either half carries the delete flag,
// duplicate keys, page cycles and short overflow chains are errors, and there
// is a lenient page-scan mode for -salvagewallet.

#include "bdbro.h"

#include "tinyformat.h"

#include <set>
#include <stdint.h>
#include <stdio.h>

#include <boost/filesystem.hpp>

namespace BerkeleyRO
{
namespace
{
// Btree metadata magic, as read little-endian from a file written by a
// little-endian machine (MAGIC) or a big-endian one (MAGIC_OE).
const uint32_t BTREE_MAGIC = 0x00053162;
const uint32_t BTREE_MAGIC_OE = 0x62310500;

// Page types (db_page.h). Only these four occur in a wallet.
const uint8_t P_IBTREE = 3;
const uint8_t P_LBTREE = 5;
const uint8_t P_OVERFLOW = 7;
const uint8_t P_BTREEMETA = 9;

// Item types.
const uint8_t B_KEYDATA = 1;
const uint8_t B_OVERFLOW = 3;
const uint8_t B_DELETE = 0x80;

// Btree metadata flags. SUBDB is the only one CDB's files carry; the others
// (duplicates, record numbers, compression...) would change the page format.
const uint32_t BTM_SUBDB = 0x20;

const size_t PAGE_HEADER_SIZE = 26;
const size_t LEAF_LEVEL = 1;

struct Meta {
    uint32_t pgno;
    uint32_t magic;
    uint32_t version;
    uint32_t pagesize;
    uint8_t encrypt_alg;
    uint8_t type;
    uint32_t last_pgno;
    uint32_t flags;
    uint32_t root;
};

struct PageHeader {
    uint32_t lsn_file;
    uint32_t lsn_offset;
    uint32_t pgno;
    uint32_t prev_pgno;
    uint32_t next_pgno;
    uint16_t entries;
    uint16_t hf_offset;
    uint8_t level;
    uint8_t type;
};

struct Item {
    bool deleted;
    Bytes data;
};

class BDBFile
{
public:
    Bytes buf;
    bool other_endian;
    uint32_t pagesize;

    BDBFile() : other_endian(false), pagesize(0) {}

    void Load(const boost::filesystem::path& path)
    {
        FILE* f = fopen(path.string().c_str(), "rb");
        if (!f)
            throw std::runtime_error("cannot open file for reading");
        unsigned char chunk[65536];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
            buf.insert(buf.end(), chunk, chunk + n);
        bool fError = ferror(f);
        fclose(f);
        if (fError)
            throw std::runtime_error("read error");
    }

    uint32_t NumPages() const { return pagesize ? buf.size() / pagesize : 0; }

    uint8_t Get8(size_t off) const
    {
        if (off >= buf.size())
            throw std::runtime_error("read past end of file");
        return buf[off];
    }
    uint16_t Get16(size_t off) const
    {
        if (off + 2 > buf.size())
            throw std::runtime_error("read past end of file");
        if (other_endian)
            return (uint16_t(buf[off]) << 8) | buf[off + 1];
        return uint16_t(buf[off]) | (uint16_t(buf[off + 1]) << 8);
    }
    uint32_t Get32(size_t off) const
    {
        if (off + 4 > buf.size())
            throw std::runtime_error("read past end of file");
        if (other_endian)
            return (uint32_t(buf[off]) << 24) | (uint32_t(buf[off + 1]) << 16) | (uint32_t(buf[off + 2]) << 8) | buf[off + 3];
        return uint32_t(buf[off]) | (uint32_t(buf[off + 1]) << 8) | (uint32_t(buf[off + 2]) << 16) | (uint32_t(buf[off + 3]) << 24);
    }

    size_t PageOffset(uint32_t pgno) const
    {
        if (pgno >= NumPages())
            throw std::runtime_error(strprintf("page %u is past the end of the file", pgno));
        return size_t(pgno) * pagesize;
    }

    Meta ReadMeta(uint32_t pgno) const
    {
        size_t base = PageOffset(pgno);
        Meta m;
        m.pgno = Get32(base + 8);
        m.magic = Get32(base + 12);
        m.version = Get32(base + 16);
        m.pagesize = Get32(base + 20);
        m.encrypt_alg = Get8(base + 24);
        m.type = Get8(base + 25);
        m.last_pgno = Get32(base + 32);
        m.flags = Get32(base + 48);
        m.root = Get32(base + 88);
        if (m.pgno != pgno)
            throw std::runtime_error(strprintf("metadata page %u claims to be page %u", pgno, m.pgno));
        if (m.magic != BTREE_MAGIC)
            throw std::runtime_error("not a Berkeley DB btree file");
        if (m.version != 9)
            throw std::runtime_error(strprintf("unsupported Berkeley DB btree version %u (expected 9)", m.version));
        if (m.pagesize != pagesize)
            throw std::runtime_error("page size differs between metadata pages");
        if (m.type != P_BTREEMETA)
            throw std::runtime_error(strprintf("page %u is not a btree metadata page", pgno));
        if (m.encrypt_alg != 0)
            throw std::runtime_error("Berkeley DB built-in encryption is not supported");
        if ((m.flags & ~BTM_SUBDB) != 0)
            throw std::runtime_error(strprintf("unsupported btree flags 0x%x", m.flags));
        return m;
    }

    PageHeader ReadHeader(uint32_t pgno) const
    {
        size_t base = PageOffset(pgno);
        PageHeader h;
        h.lsn_file = Get32(base + 0);
        h.lsn_offset = Get32(base + 4);
        h.pgno = Get32(base + 8);
        h.prev_pgno = Get32(base + 12);
        h.next_pgno = Get32(base + 16);
        h.entries = Get16(base + 20);
        h.hf_offset = Get16(base + 22);
        h.level = Get8(base + 24);
        h.type = Get8(base + 25);
        if (h.pgno != pgno)
            throw std::runtime_error(strprintf("page %u claims to be page %u", pgno, h.pgno));
        return h;
    }

    /** Offset (from page start) of item i, checked to lie inside the page. */
    size_t ItemOffset(uint32_t pgno, const PageHeader& h, unsigned int i, size_t minlen) const
    {
        size_t base = PageOffset(pgno);
        size_t idxpos = PAGE_HEADER_SIZE + 2 * size_t(i);
        if (idxpos + 2 > pagesize)
            throw std::runtime_error(strprintf("page %u: index table overflows the page", pgno));
        size_t off = Get16(base + idxpos);
        if (off < PAGE_HEADER_SIZE + 2 * size_t(h.entries) || off + minlen > pagesize)
            throw std::runtime_error(strprintf("page %u: item %u lies outside the page", pgno, i));
        return off;
    }

    Bytes ReadOverflow(uint32_t first, uint32_t total_len) const
    {
        Bytes out;
        std::set<uint32_t> seen;
        uint32_t pgno = first;
        while (pgno != 0) {
            if (!seen.insert(pgno).second)
                throw std::runtime_error(strprintf("overflow chain loops at page %u", pgno));
            PageHeader h = ReadHeader(pgno);
            if (h.type != P_OVERFLOW || h.level != 0)
                throw std::runtime_error(strprintf("page %u should be an overflow page", pgno));
            if (PAGE_HEADER_SIZE + size_t(h.hf_offset) > pagesize)
                throw std::runtime_error(strprintf("overflow page %u holds more than a page", pgno));
            size_t base = PageOffset(pgno) + PAGE_HEADER_SIZE;
            out.insert(out.end(), buf.begin() + base, buf.begin() + base + h.hf_offset);
            if (out.size() > total_len)
                throw std::runtime_error("overflow chain is longer than its item");
            pgno = h.next_pgno;
        }
        if (out.size() != total_len)
            throw std::runtime_error(strprintf("overflow item is %u bytes, chain holds %u", total_len, (unsigned int)out.size()));
        return out;
    }

    /** Item i of a leaf page. Throws if that one item is unreadable. */
    Item ReadLeafItem(uint32_t pgno, const PageHeader& h, unsigned int i) const
    {
        size_t base = PageOffset(pgno);
        size_t off = ItemOffset(pgno, h, i, 3);
        uint16_t len = Get16(base + off);
        uint8_t type = Get8(base + off + 2);
        Item item;
        item.deleted = (type & B_DELETE) != 0;
        type &= ~B_DELETE;
        if (type == B_KEYDATA) {
            if (off + 3 + len > pagesize)
                throw std::runtime_error(strprintf("page %u: item %u runs off the page", pgno, i));
            item.data.assign(buf.begin() + base + off + 3, buf.begin() + base + off + 3 + len);
        } else if (type == B_OVERFLOW) {
            if (off + 12 > pagesize)
                throw std::runtime_error(strprintf("page %u: overflow item %u runs off the page", pgno, i));
            uint32_t first = Get32(base + off + 4);
            uint32_t total = Get32(base + off + 8);
            if (!item.deleted)
                item.data = ReadOverflow(first, total);
        } else {
            throw std::runtime_error(strprintf("page %u: unsupported item type %u", pgno, type));
        }
        return item;
    }

    /** All items of a leaf page, in index order. */
    std::vector<Item> ReadLeafItems(uint32_t pgno, const PageHeader& h) const
    {
        std::vector<Item> items;
        for (unsigned int i = 0; i < h.entries; i++)
            items.push_back(ReadLeafItem(pgno, h, i));
        return items;
    }

    /** Child page numbers of an internal page. */
    std::vector<uint32_t> ReadInternalChildren(uint32_t pgno, const PageHeader& h) const
    {
        std::vector<uint32_t> children;
        size_t base = PageOffset(pgno);
        for (unsigned int i = 0; i < h.entries; i++) {
            size_t off = ItemOffset(pgno, h, i, 12);
            uint16_t len = Get16(base + off);
            uint8_t type = Get8(base + off + 2);
            if ((type & ~B_DELETE) != B_KEYDATA)
                throw std::runtime_error(strprintf("page %u: unsupported internal item type %u", pgno, type));
            if (off + 12 + len > pagesize)
                throw std::runtime_error(strprintf("page %u: internal item %u runs off the page", pgno, i));
            if (type & B_DELETE)
                continue;
            children.push_back(Get32(base + off + 4));
        }
        return children;
    }

    /** Open the file and validate the outer metadata page. */
    Meta Open(const boost::filesystem::path& path)
    {
        Load(path);
        if (buf.size() < 512)
            throw std::runtime_error("file is too small to be a Berkeley DB database");
        // Endianness comes from the magic, the page size from the first page.
        uint32_t magic = uint32_t(buf[12]) | (uint32_t(buf[13]) << 8) | (uint32_t(buf[14]) << 16) | (uint32_t(buf[15]) << 24);
        if (magic == BTREE_MAGIC)
            other_endian = false;
        else if (magic == BTREE_MAGIC_OE)
            other_endian = true;
        else
            throw std::runtime_error("not a Berkeley DB btree file");
        pagesize = Get32(20);
        if (pagesize < 512 || pagesize > 65536 || (pagesize & (pagesize - 1)) != 0)
            throw std::runtime_error(strprintf("bad page size %u", pagesize));
        return ReadMeta(0);
    }
};

/** Decode one leaf page into key/value pairs. Pairs with a deleted half are dropped. */
void LeafPairs(const BDBFile& f, uint32_t pgno, const PageHeader& h, RecordList& out)
{
    std::vector<Item> items = f.ReadLeafItems(pgno, h);
    if (items.size() % 2 != 0)
        throw std::runtime_error(strprintf("page %u: odd number of items on a leaf page", pgno));
    for (size_t i = 0; i < items.size(); i += 2) {
        if (items[i].deleted || items[i + 1].deleted)
            continue;
        out.push_back(std::make_pair(items[i].data, items[i + 1].data));
    }
}

/** Page number of the "main" subdatabase's metadata page, from the outer root leaf. */
uint32_t FindMainSubdb(const BDBFile& f, const Meta& outer)
{
    PageHeader h = f.ReadHeader(outer.root);
    if (h.type != P_LBTREE)
        throw std::runtime_error("outer database root is not a leaf page");
    if (h.entries != 2)
        throw std::runtime_error("outer database should hold exactly one subdatabase");
    RecordList pairs;
    LeafPairs(f, outer.root, h, pairs);
    static const unsigned char name[] = {'m', 'a', 'i', 'n'};
    if (pairs.size() != 1 || pairs[0].first != Bytes(name, name + 4))
        throw std::runtime_error("subdatabase is not called \"main\"");
    if (pairs[0].second.size() != 4)
        throw std::runtime_error("subdatabase page number has an unexpected length");
    const Bytes& v = pairs[0].second; // always big-endian
    return (uint32_t(v[0]) << 24) | (uint32_t(v[1]) << 16) | (uint32_t(v[2]) << 8) | v[3];
}
} // anonymous namespace

bool IsBerkeleyBtreeFile(const boost::filesystem::path& path)
{
    FILE* f = fopen(path.string().c_str(), "rb");
    if (!f)
        return false;
    unsigned char hdr[16];
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (n != sizeof(hdr))
        return false;
    uint32_t magic = uint32_t(hdr[12]) | (uint32_t(hdr[13]) << 8) | (uint32_t(hdr[14]) << 16) | (uint32_t(hdr[15]) << 24);
    return magic == BTREE_MAGIC || magic == BTREE_MAGIC_OE;
}

void ReadAll(const boost::filesystem::path& path, RecordMap& records)
{
    records.clear();
    BDBFile f;
    Meta outer = f.Open(path);

    // The file must end exactly where the metadata says. BDB does not keep the
    // size a multiple of the page size, so only whole pages are counted.
    if (outer.last_pgno != f.NumPages() - 1)
        throw std::runtime_error(strprintf("file holds %u pages but the metadata says %u (truncated or extended?)",
                                           f.NumPages(), outer.last_pgno + 1));
    if (outer.flags != BTM_SUBDB)
        throw std::runtime_error("file has no subdatabases (not a wallet written by CDB)");

    // A cleanly closed wallet has every page LSN reset to [0][1]. Anything else
    // means data may still live in the database/ log directory, which only
    // Berkeley DB can replay.
    for (uint32_t i = 0; i <= outer.last_pgno; i++) {
        size_t base = f.PageOffset(i);
        if (f.Get32(base) != 0 || f.Get32(base + 4) != 1)
            throw std::runtime_error("the file was not closed cleanly by the Berkeley DB version that wrote it "
                                     "(page LSNs are not reset); start that version once, shut it down "
                                     "normally, then try again");
    }

    uint32_t main_pgno = FindMainSubdb(f, outer);
    if (main_pgno > outer.last_pgno)
        throw std::runtime_error("subdatabase metadata page is past the end of the file");
    Meta inner = f.ReadMeta(main_pgno);

    std::set<uint32_t> visited;
    std::vector<std::pair<uint32_t, int> > stack; // page, expected level (-1: any)
    stack.push_back(std::make_pair(inner.root, -1));
    while (!stack.empty()) {
        uint32_t pgno = stack.back().first;
        int expected_level = stack.back().second;
        stack.pop_back();
        if (!visited.insert(pgno).second)
            throw std::runtime_error(strprintf("page %u is referenced twice", pgno));
        PageHeader h = f.ReadHeader(pgno);
        if (expected_level >= 0 && h.level != expected_level)
            throw std::runtime_error(strprintf("page %u has btree level %u, expected %d", pgno, h.level, expected_level));
        if (h.type == P_IBTREE) {
            if (h.level <= LEAF_LEVEL)
                throw std::runtime_error(strprintf("internal page %u has leaf level", pgno));
            std::vector<uint32_t> children = f.ReadInternalChildren(pgno, h);
            for (size_t i = 0; i < children.size(); i++)
                stack.push_back(std::make_pair(children[i], int(h.level) - 1));
        } else if (h.type == P_LBTREE) {
            if (h.level != LEAF_LEVEL)
                throw std::runtime_error(strprintf("leaf page %u has level %u", pgno, h.level));
            RecordList pairs;
            LeafPairs(f, pgno, h, pairs);
            for (size_t i = 0; i < pairs.size(); i++) {
                if (!records.insert(pairs[i]).second)
                    throw std::runtime_error("duplicate key in database");
            }
        } else {
            throw std::runtime_error(strprintf("page %u has unexpected type %u inside the btree", pgno, h.type));
        }
    }
}

bool Salvage(const boost::filesystem::path& path, RecordList& records, std::string& strReport)
{
    records.clear();
    try {
        RecordMap m;
        ReadAll(path, m);
        records.assign(m.begin(), m.end());
        strReport = strprintf("file read cleanly, %u records", (unsigned int)records.size());
        return true;
    } catch (const std::exception& e) {
        strReport = strprintf("clean read failed (%s); scanning pages. ", e.what());
    }

    BDBFile f;
    try {
        f.Load(path);
    } catch (const std::exception& e) {
        strReport += e.what();
        return false;
    }
    if (f.buf.size() < 512) {
        strReport += "file too small";
        return false;
    }
    uint32_t magic = uint32_t(f.buf[12]) | (uint32_t(f.buf[13]) << 8) | (uint32_t(f.buf[14]) << 16) | (uint32_t(f.buf[15]) << 24);
    f.other_endian = (magic == BTREE_MAGIC_OE);
    f.pagesize = f.Get32(20);
    if (f.pagesize < 512 || f.pagesize > 65536 || (f.pagesize & (f.pagesize - 1)) != 0) {
        f.pagesize = 4096; // what CDB always used; worth a try when page 0 is damaged
        strReport += "page 0 unreadable, assuming 4096-byte pages. ";
    }

    static const unsigned char name[] = {'m', 'a', 'i', 'n'};
    const Bytes subdb_name(name, name + 4);
    std::set<Bytes> seen;
    unsigned int nPagesRead = 0, nPagesSkipped = 0, nItemsSkipped = 0, nDupes = 0;
    for (uint32_t pgno = 1; pgno < f.NumPages(); pgno++) {
        try {
            PageHeader h = f.ReadHeader(pgno);
            if (h.type != P_LBTREE || h.level != LEAF_LEVEL)
                continue;
            // Pair by pair, not LeafPairs: one damaged item costs its own
            // key/value pair, not every record on the page. Keys sit at even
            // indexes and values at odd ones, so a skipped pair does not shift
            // the pairing of the rest.
            RecordList pairs;
            for (unsigned int i = 0; i + 1 < h.entries; i += 2) {
                try {
                    Item k = f.ReadLeafItem(pgno, h, i);
                    Item v = f.ReadLeafItem(pgno, h, i + 1);
                    if (!k.deleted && !v.deleted)
                        pairs.push_back(std::make_pair(k.data, v.data));
                } catch (const std::exception&) {
                    nItemsSkipped++;
                }
            }
            nPagesRead++;
            for (size_t i = 0; i < pairs.size(); i++) {
                if (pairs[i].first == subdb_name && pairs[i].second.size() == 4)
                    continue; // the outer database's pointer to "main", not a wallet record
                if (!seen.insert(pairs[i].first).second) {
                    nDupes++;
                    continue;
                }
                records.push_back(pairs[i]);
            }
        } catch (const std::exception&) {
            nPagesSkipped++;
        }
    }
    strReport += strprintf("%u leaf pages read, %u unreadable pages skipped, %u unreadable records skipped, %u repeated keys dropped, %u records recovered",
                           nPagesRead, nPagesSkipped, nItemsSkipped, nDupes, (unsigned int)records.size());
    return !records.empty();
}
} // namespace BerkeleyRO

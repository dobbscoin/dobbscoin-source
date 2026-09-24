// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "fuzz_util.h"

#include "chainparams.h"
#include "util.h"
#include "utiltime.h"

#include <algorithm>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace fuzz
{
namespace
{
fs::path g_dir;
fs::path g_side;

void RemoveScratch()
{
    boost::system::error_code ec;
    if (!g_dir.empty())
        fs::remove_all(g_dir, ec);
    if (!g_side.empty())
        fs::remove_all(g_side, ec);
}

fs::path ScratchBase()
{
    const char* env = getenv("BOB_FUZZ_TMPDIR");
    if (env && *env)
        return fs::path(env);
    boost::system::error_code ec;
    if (fs::is_directory("/dev/shm", ec) && access("/dev/shm", W_OK) == 0)
        return fs::path("/dev/shm"); // tmpfs: SQLite's fsyncs cost nothing there
    return fs::temp_directory_path();
}

/** Remove "dobbscoin-fuzz-*-<pid>[-side]" directories whose process is gone. */
void RemoveStale(const fs::path& base)
{
    boost::system::error_code ec;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.compare(0, 15, "dobbscoin-fuzz-") != 0)
            continue;
        std::string rest = name;
        if (rest.size() > 5 && rest.compare(rest.size() - 5, 5, "-side") == 0)
            rest.resize(rest.size() - 5);
        size_t dash = rest.rfind('-');
        if (dash == std::string::npos)
            continue;
        long pid = atol(rest.c_str() + dash + 1);
        if (pid <= 0 || (kill((pid_t)pid, 0) != 0 && errno == ESRCH)) {
            boost::system::error_code ec2;
            fs::remove_all(it->path(), ec2);
        }
    }
}
} // namespace

const fs::path& Setup(const std::string& strName)
{
    if (!g_dir.empty())
        return g_dir;
    fs::path base = ScratchBase();
    RemoveStale(base);
    std::string stem = strprintf("dobbscoin-fuzz-%s-%d", strName, (int)getpid());
    g_dir = base / stem;
    g_side = base / (stem + "-side");
    fs::create_directories(g_dir);
    fs::create_directories(g_side);
    atexit(RemoveScratch);

    fPrintToDebugLog = false; // never open a debug.log anywhere
    fPrintToConsole = false;
    SelectParams(CBaseChainParams::MAIN);
    mapArgs["-datadir"] = g_dir.string();
    if (GetDataDir() != fs::system_complete(g_dir)) {
        fprintf(stderr, "fuzz: GetDataDir() is %s, expected %s\n", GetDataDir().string().c_str(), g_dir.string().c_str());
        abort();
    }
    g_dir = GetDataDir();
    SetMockTime(MOCK_TIME);
    return g_dir;
}

const fs::path& SideDir()
{
    return g_side;
}

void WriteFile(const fs::path& p, const uint8_t* data, size_t size)
{
    FILE* f = fopen(p.string().c_str(), "wb");
    if (!f || (size && fwrite(data, 1, size, f) != size) || fclose(f) != 0) {
        fprintf(stderr, "fuzz: cannot write %s\n", p.string().c_str());
        abort();
    }
}

Bytes ReadFile(const fs::path& p)
{
    Bytes b;
    FILE* f = fopen(p.string().c_str(), "rb");
    if (!f)
        return b;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        b.insert(b.end(), buf, buf + n);
    fclose(f);
    return b;
}

std::vector<std::string> List(const fs::path& dir)
{
    std::vector<std::string> v;
    for (fs::directory_iterator it(dir), end; it != end; ++it)
        v.push_back(it->path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

void Clear(const fs::path& dir)
{
    for (fs::directory_iterator it(dir), end; it != end;) {
        fs::path p = it->path();
        ++it;
        fs::remove_all(p);
    }
}

std::string Join(const std::vector<std::string>& v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); i++)
        s += (i ? " " : "") + v[i];
    return s;
}

void Fail(const char* file, int line, const char* cond, const std::string& msg)
{
    fprintf(stderr, "\n==== FUZZ INVARIANT FAILED ====\n%s:%d: %s\n%s\n", file, line, cond, msg.c_str());
    fflush(stderr);
    abort();
}
} // namespace fuzz

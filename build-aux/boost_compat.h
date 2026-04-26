#ifndef DOBBSCOIN_BUILD_AUX_BOOST_COMPAT_H
#define DOBBSCOIN_BUILD_AUX_BOOST_COMPAT_H

#include <boost/version.hpp>
#include <boost/bind/placeholders.hpp>

#if BOOST_VERSION < 105600
namespace boost {
namespace placeholders {
using ::_1;
using ::_2;
using ::_3;
using ::_4;
using ::_5;
using ::_6;
using ::_7;
using ::_8;
using ::_9;
} // namespace placeholders
} // namespace boost
#endif

#endif // DOBBSCOIN_BUILD_AUX_BOOST_COMPAT_H

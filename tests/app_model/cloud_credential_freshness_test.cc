#include "test_framework.h"

#include "phone_os/cloud_credential_freshness.h"

using rodakos::CloudCredentialFreshness;

RODAK_TEST("Cloud credentials require verification after every boot") {
    CloudCredentialFreshness freshness;
    RODAK_CHECK(freshness.NeedsRefresh(0));
    RODAK_CHECK(freshness.NeedsRefresh(100000));
    RODAK_CHECK_EQ(freshness.expires_at_ms(), 0);
}

RODAK_TEST("Cloud token freshness uses a controlled monotonic expiry with margin") {
    CloudCredentialFreshness freshness;
    freshness.ObserveRefresh(true, 10000, 600);
    RODAK_CHECK(!freshness.NeedsRefresh(579999));
    RODAK_CHECK(freshness.NeedsRefresh(580000));
    RODAK_CHECK_EQ(freshness.expires_at_ms(), 610000);
}

RODAK_TEST("A failed refresh cannot extend or retain verified freshness") {
    CloudCredentialFreshness freshness;
    freshness.ObserveRefresh(true, 0, 600);
    freshness.ObserveRefresh(false, 590000, 600);
    RODAK_CHECK(freshness.NeedsRefresh(590000));
    RODAK_CHECK_EQ(freshness.expires_at_ms(), 0);
    freshness.ObserveRefresh(true, 591000, 600);
    RODAK_CHECK(!freshness.NeedsRefresh(591000));
    RODAK_CHECK_EQ(freshness.expires_at_ms(), 1191000);
}

RODAK_TEST("Missing and invalid token lifetimes never become a reusable cache") {
    CloudCredentialFreshness freshness;
    for (int lifetime : {0, -1}) {
        freshness.ObserveRefresh(true, 1000, lifetime);
        RODAK_CHECK(freshness.NeedsRefresh(1000));
        RODAK_CHECK_EQ(freshness.expires_at_ms(), 0);
    }
    freshness.ObserveRefresh(true, -1, 600);
    RODAK_CHECK(freshness.NeedsRefresh(0));
}

RODAK_TEST("Short token lifetimes retain a usable window without overflow") {
    CloudCredentialFreshness freshness;
    freshness.ObserveRefresh(true, 0, 1);
    RODAK_CHECK(!freshness.NeedsRefresh(999));
    RODAK_CHECK(freshness.NeedsRefresh(1000));
    freshness.ObserveRefresh(true, 1000, INT32_MAX);
    RODAK_CHECK_EQ(freshness.expires_at_ms(), 2147483648000LL);
}

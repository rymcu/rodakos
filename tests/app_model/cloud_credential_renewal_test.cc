#include "test_framework.h"

#include "phone_os/cloud_credential_renewal.h"

using rodakos::CloudCredentialRenewal;

RODAK_TEST("Credential renewal is not due before any token is observed") {
    CloudCredentialRenewal renewal;
    RODAK_CHECK(!renewal.Due(0));
    RODAK_CHECK(!renewal.Due(INT64_MAX / 2));
}

RODAK_TEST("A 600 s token renews 150 s before expiry, inside the 30 s freshness margin") {
    CloudCredentialRenewal renewal;
    renewal.ObserveToken(10000, 610000);
    RODAK_CHECK_EQ(renewal.due_at_ms(), 460000);
    RODAK_CHECK(!renewal.Due(459999));
    RODAK_CHECK(renewal.Due(460000));
    // 语音前的新鲜度检查在到期前 30 s 才要求刷新；续期必须更早。
    RODAK_CHECK(renewal.due_at_ms() < 610000 - 30000);
}

RODAK_TEST("Renewal lead is clamped for long and short token lifetimes") {
    CloudCredentialRenewal renewal;
    renewal.ObserveToken(0, 3600000);
    RODAK_CHECK_EQ(renewal.due_at_ms(), 3600000 - 300000);
    renewal.ObserveToken(0, 120000);
    RODAK_CHECK_EQ(renewal.due_at_ms(), 60000);
    renewal.ObserveToken(0, 20000);
    RODAK_CHECK_EQ(renewal.due_at_ms(), 10000);
}

RODAK_TEST("An expired or missing expiry never schedules a renewal") {
    CloudCredentialRenewal renewal;
    renewal.ObserveToken(5000, 5000);
    RODAK_CHECK(!renewal.Due(5000));
    renewal.ObserveToken(5000, 0);
    RODAK_CHECK(!renewal.Due(1000000));
}

RODAK_TEST("Failed renewals back off exponentially up to 8 minutes") {
    CloudCredentialRenewal renewal;
    renewal.ObserveToken(0, 600000);
    int64_t now = renewal.due_at_ms();
    const int64_t expected[] = {15000, 30000, 60000, 120000, 240000, 480000, 480000};
    for (int64_t wait : expected) {
        RODAK_CHECK(renewal.Due(now));
        renewal.ObserveFailure(now);
        RODAK_CHECK_EQ(renewal.retry_at_ms(), now + wait);
        RODAK_CHECK(!renewal.Due(now + wait - 1));
        now += wait;
    }
    RODAK_CHECK(renewal.Due(now));
}

RODAK_TEST("A new token resets the backoff and the schedule") {
    CloudCredentialRenewal renewal;
    renewal.ObserveToken(0, 600000);
    renewal.ObserveFailure(450000);
    renewal.ObserveFailure(465000);
    renewal.ObserveToken(500000, 1100000);
    RODAK_CHECK_EQ(renewal.due_at_ms(), 950000);
    RODAK_CHECK_EQ(renewal.retry_at_ms(), 0);
    RODAK_CHECK(!renewal.Due(949999));
    RODAK_CHECK(renewal.Due(950000));
}

RODAK_TEST("Clearing the renewal stops it until a new token is observed") {
    CloudCredentialRenewal renewal;
    renewal.ObserveToken(0, 600000);
    renewal.Clear();
    RODAK_CHECK(!renewal.Due(1000000));
}

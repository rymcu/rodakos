#pragma once

#include <cstdint>

class PhoneUi {
public:
    uint32_t theme_revision() const { return theme_revision_; }
    void SetThemeRevision(uint32_t revision) { theme_revision_ = revision; }
    void AdvanceThemeRevision() { ++theme_revision_; }

    void ResetInputState() { ++input_reset_count_; }
    int input_reset_count() const { return input_reset_count_; }

private:
    uint32_t theme_revision_ = 0;
    int input_reset_count_ = 0;
};

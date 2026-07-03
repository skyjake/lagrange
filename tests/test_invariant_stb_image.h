#include <check.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "src/stb_image.h"

START_TEST(test_crafted_image_no_oob_access)
{
    // Invariant: stbi_load_from_memory must not crash or corrupt memory
    // when given crafted images with malicious dimensions. It should either
    // return a valid image or NULL with no memory corruption.

    // Minimal valid 1x1 BMP (valid input)
    unsigned char valid_bmp[] = {
        0x42,0x4D,0x3E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,
        0x28,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,
        0x18,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
        0x00,0x00
    };

    // Crafted BMP with huge dimensions (triggers large bytes_copy in row swap)
    unsigned char huge_dim_bmp[] = {
        0x42,0x4D,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,
        0x28,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0x00,0x00,0x01,0x00,
        0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };

    // Truncated header (boundary: incomplete data)
    unsigned char truncated[] = {0x42,0x4D,0x00,0x00,0x00};

    struct { unsigned char *data; int len; } cases[] = {
        {valid_bmp, sizeof(valid_bmp)},
        {huge_dim_bmp, sizeof(huge_dim_bmp)},
        {truncated, sizeof(truncated)},
    };

    for (int i = 0; i < 3; i++) {
        int w = 0, h = 0, channels = 0;
        unsigned char *img = stbi_load_from_memory(
            cases[i].data, cases[i].len, &w, &h, &channels, 0);
        if (img) {
            // If image loaded, dimensions must be sane and positive
            ck_assert_int_gt(w, 0);
            ck_assert_int_gt(h, 0);
            ck_assert_int_gt(channels, 0);
            ck_assert_int_le(channels, 4);
            // Verify we can read the entire buffer without crash
            volatile unsigned char sum = 0;
            for (int j = 0; j < w * h * channels; j++)
                sum += img[j];
            (void)sum;
            stbi_image_free(img);
        }
        // If NULL returned, that's safe — no crash means invariant holds
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s = suite_create("Security");
    TCase *tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 10);
    tcase_add_test(tc_core, test_crafted_image_no_oob_access);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void)
{
    Suite *s = security_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0
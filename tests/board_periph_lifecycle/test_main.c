#include "esp_board_periph.h"
#include "esp_board_manager_err.h"

#include <stdio.h>
#include <string.h>

static int init_calls;
static int deinit_calls;
static esp_err_t deinit_result;
static int peripheral_token;
static const char peripheral_config[] = "host-config";

static esp_err_t fake_init(void *config, int config_size, void **handle)
{
    if (config != peripheral_config || config_size != (int)sizeof(peripheral_config) || handle == NULL) {
        return ESP_FAIL;
    }
    ++init_calls;
    *handle = &peripheral_token;
    return ESP_OK;
}

static esp_err_t fake_deinit(void *handle)
{
    if (handle != &peripheral_token) {
        return ESP_FAIL;
    }
    ++deinit_calls;
    return deinit_result;
}

const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {NULL, "host_peripheral", "host", ESP_BOARD_PERIPH_ROLE_NONE, NULL,
     peripheral_config, (int)sizeof(peripheral_config), 0},
    {NULL, NULL, NULL, ESP_BOARD_PERIPH_ROLE_NONE, NULL, NULL, 0, 0},
};

esp_board_periph_entry_t g_esp_board_periph_handles[] = {
    {NULL, "host", ESP_BOARD_PERIPH_ROLE_NONE, fake_init, fake_deinit},
    {NULL, NULL, ESP_BOARD_PERIPH_ROLE_NONE, NULL, NULL},
};

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void reset_state(void)
{
    init_calls = 0;
    deinit_calls = 0;
    deinit_result = ESP_FAIL;
    g_esp_board_periph_handles[0].deinit = fake_deinit;
}

static int run_deinit_failure_retry(void)
{
    void *handle = NULL;
    check(esp_board_periph_ref_handle("host_peripheral", &handle) == ESP_OK,
          "initial peripheral reference succeeds");
    check(handle == &peripheral_token && init_calls == 1, "initial handle and init callback");

    check(esp_board_periph_unref_handle("host_peripheral") == ESP_FAIL,
          "first deinit failure is propagated");
    check(deinit_calls == 1, "first deinit callback runs");

    deinit_result = ESP_OK;
    check(esp_board_periph_unref_handle("host_peripheral") == ESP_OK,
          "retry deinit succeeds");
    check(deinit_calls == 2, "retry invokes the real deinit callback");
    check(esp_board_periph_get_handle("host_peripheral", &handle) != ESP_OK,
          "successful retry clears the peripheral handle");
    return failures == 0 ? 0 : 1;
}

static int run_missing_callback_retry(void)
{
    void *handle = NULL;
    check(esp_board_periph_ref_handle("host_peripheral", &handle) == ESP_OK,
          "initial peripheral reference succeeds");
    g_esp_board_periph_handles[0].deinit = NULL;
    check(esp_board_periph_unref_handle("host_peripheral") == ESP_BOARD_ERR_PERIPH_NO_INIT,
          "missing deinit callback is propagated");
    check(deinit_calls == 0, "missing callback performs no deinit");

    g_esp_board_periph_handles[0].deinit = fake_deinit;
    deinit_result = ESP_OK;
    check(esp_board_periph_unref_handle("host_peripheral") == ESP_OK,
          "missing callback retry succeeds");
    check(deinit_calls == 1, "callback retry invokes real deinit");
    check(esp_board_periph_get_handle("host_peripheral", &handle) != ESP_OK,
          "callback retry clears the peripheral handle");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    check(argc == 2, "scenario argument required");
    if (argc != 2) {
        return 1;
    }
    reset_state();
    if (strcmp(argv[1], "deinit_failure_retry") == 0) {
        return run_deinit_failure_retry();
    }
    if (strcmp(argv[1], "missing_callback_retry") == 0) {
        return run_missing_callback_retry();
    }
    fprintf(stderr, "FAIL: unknown scenario\n");
    return 1;
}

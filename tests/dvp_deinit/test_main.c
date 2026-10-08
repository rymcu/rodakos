#include "host_fakes.h"
#include RODAK_DVP_SOURCE

enum operation { LOOKUP, VIDEO, SCCB, SENSOR, PORT, JPEG_CREATE, JPEG_DELETE, CREATE, OP_COUNT };
struct host_sccb { unsigned marker; };
struct allocation { void *pointer; bool live; bool sensor; };
static struct allocation allocations[32];
static size_t allocation_count;
static unsigned calls[OP_COUNT];
static int fail_operation = -1;
static unsigned failures_remaining;
static unsigned invalid_accesses;
static unsigned locks;
static unsigned initializations;
static bool video_live, port_live, jpeg_live;
static bool fail_cleanup_sccb;
static esp_cam_sensor_device_t *published_sensor;
static const char *scenario;
static esp_video_init_dvp_config_t dvp_config = { .reset_pin = -1, .pwdn_pin = -1 };
static esp_video_init_config_t config = { .dvp = &dvp_config };

#define CHECK(value, message) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s: %s\n", scenario, message); return 1; } } while (0)

static bool live_pointer(const void *pointer, bool sensor)
{
    for (size_t i = 0; i < allocation_count; ++i)
        if (allocations[i].pointer == pointer && allocations[i].sensor == sensor)
            return allocations[i].live;
    return false;
}

static bool require_live(const void *pointer, bool sensor)
{
    if (live_pointer(pointer, sensor)) return true;
    ++invalid_accesses;
    fprintf(stderr, "Rejected access to retired %s handle\n", sensor ? "sensor" : "SCCB");
    return false;
}

static void *allocate_handle(size_t bytes, bool sensor)
{
    if (allocation_count == 32) abort();
    void *pointer = calloc(1, bytes);
    if (pointer == NULL) abort();
    allocations[allocation_count++] = (struct allocation){ pointer, true, sensor };
    return pointer;
}

static void retire_handle(void *pointer, bool sensor)
{
    if (!require_live(pointer, sensor)) abort();
    for (size_t i = 0; i < allocation_count; ++i)
        if (allocations[i].pointer == pointer) allocations[i].live = false;
    /* Tombstones are poisoned and every fake access checks lifetime before
     * dereferencing. This yields a specified failure, never a UAF crash pass. */
    memset(pointer, 0xa5, sensor ? sizeof(esp_cam_sensor_device_t) : sizeof(struct host_sccb));
}

static bool fail(enum operation operation)
{
    if (locks == 0) abort();
    ++calls[operation];
    if (fail_operation != (int)operation || failures_remaining == 0) return false;
    --failures_remaining;
    return true;
}

static void inject(enum operation operation)
{
    fail_operation = operation;
    failures_remaining = 1;
}

void _lock_acquire_recursive(_lock_t *lock) { ++locks; }
void _lock_release_recursive(_lock_t *lock) { if (locks == 0) abort(); --locks; }
esp_err_t gpio_reset_pin(int pin) { return ESP_OK; }
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) { abort(); }
esp_err_t esp_cam_ctlr_dvp_init_ext(int id, int source, const host_dvp_pin_t *pins)
{
    if (locks == 0 || port_live) abort();
    port_live = true;
    return ESP_OK;
}
esp_err_t esp_cam_ctlr_dvp_output_clock(int id, int source, int hz) { return ESP_OK; }
esp_err_t esp_cam_ctlr_dvp_deinit(int id)
{
    if (fail(PORT)) return ESP_FAIL;
    if (!port_live) { ++invalid_accesses; return ESP_ERR_INVALID_STATE; }
    port_live = false;
    return ESP_OK;
}
esp_err_t esp_video_device_common_get_video_cam(const char *name, esp_video_cam_t *cam)
{
    if (fail(LOOKUP)) return ESP_FAIL;
    if (!video_live) { ++invalid_accesses; return ESP_ERR_INVALID_STATE; }
    if (!require_live(published_sensor, true)) return ESP_ERR_INVALID_STATE;
    cam->sensor = published_sensor;
    return ESP_OK;
}
esp_err_t esp_video_destroy_dvp_video_device(void)
{
    if (fail(VIDEO)) return ESP_ERR_NO_MEM;
    if (!video_live) { ++invalid_accesses; return ESP_ERR_INVALID_STATE; }
    video_live = false;
    published_sensor = NULL;
    return ESP_OK;
}
esp_err_t esp_sccb_del_i2c_io(esp_sccb_io_handle_t sccb)
{
    if (!require_live(sccb, false)) return ESP_ERR_INVALID_STATE;
    if (fail_cleanup_sccb) {
        fail_cleanup_sccb = false;
        if (locks == 0) abort();
        ++calls[SCCB];
        return ESP_FAIL;
    }
    if (fail(SCCB)) return ESP_FAIL;
    retire_handle(sccb, false);
    return ESP_OK;
}
esp_err_t esp_cam_sensor_del_dev(esp_cam_sensor_device_t *sensor)
{
    if (!require_live(sensor, true)) return ESP_ERR_INVALID_STATE;
    if (fail(SENSOR)) return ESP_FAIL;
    retire_handle(sensor, true);
    return ESP_OK;
}
esp_err_t esp_video_create_dvp_video_device(esp_cam_sensor_device_t *sensor)
{
    if (fail(CREATE)) return ESP_FAIL;
    if (video_live || !require_live(sensor, true)) abort();
    published_sensor = sensor;
    video_live = true;
    return ESP_OK;
}
esp_err_t esp_video_create_jpeg_enc_video_device(jpeg_encoder_handle_t handle)
{
    if (fail(JPEG_CREATE)) return ESP_FAIL;
    if (jpeg_live) abort();
    jpeg_live = true;
    return ESP_OK;
}
esp_err_t esp_video_destroy_jpeg_enc_video_device(void)
{
    if (fail(JPEG_DELETE)) return ESP_FAIL;
    if (!jpeg_live) abort();
    jpeg_live = false;
    return ESP_OK;
}
void esp_cam_sensor_detect_get_array(esp_cam_sensor_detect_fn_t **start,
                                    esp_cam_sensor_detect_fn_t **end)
{
    static esp_cam_sensor_detect_fn_t detectors[] = { { ESP_CAM_SENSOR_DVP } };
    *start = detectors; *end = detectors + 1;
}
esp_err_t host_initialize(const video_device_init_config_t *cfg)
{
    ++initializations;
    esp_err_t ret = cfg->init_clk_func(cfg->clk_priv);
    if (ret != ESP_OK) return ret;
    esp_cam_sensor_device_t *sensor = allocate_handle(sizeof(*sensor), true);
    sensor->reset_pin = sensor->pwdn_pin = -1;
    sensor->sccb_handle = allocate_handle(sizeof(struct host_sccb), false);
    ret = cfg->create_func(sensor, cfg->create_priv);
    if (ret != ESP_OK) {
        retire_handle(sensor->sccb_handle, false);
        retire_handle(sensor, true);
        (void)cfg->deinit_clk_func(cfg->clk_priv);
        return ret;
    }
    *cfg->device_inited = 1;
    return ESP_OK;
}

static bool all_released(void)
{
    if (video_live || port_live || jpeg_live || locks != 0 || s_video_device_inited_flags != 0)
        return false;
    for (size_t i = 0; i < allocation_count; ++i)
        if (allocations[i].live) return false;
    return invalid_accesses == 0;
}

static int run(void)
{
    if (strcmp(scenario, "create_failure") == 0) {
        inject(CREATE);
        CHECK(esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_DVP) == ESP_FAIL,
              "initial creation failure must propagate");
        CHECK(all_released(), "failed creation must not create a cleanup context");
        CHECK(esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_DVP) == ESP_OK,
              "fresh lifecycle after failed creation");
        CHECK(esp_video_deinit() == ESP_OK, "cleanup after fresh creation");
        CHECK(all_released(), "fresh lifecycle must be independent");
        return 0;
    }
    if (strcmp(scenario, "init_cleanup_failure") == 0) {
        /* A later component fails after DVP was created. Its cleanup enters the
         * actual deinit-with-flags path; the retained context must fence init. */
        inject(JPEG_CREATE);
        fail_cleanup_sccb = true;
        CHECK(esp_video_init(&config) == ESP_FAIL, "later init failure must propagate");
        CHECK(!video_live && port_live, "failed init cleanup retains downstream ownership");
        CHECK(esp_video_init(&config) == ESP_ERR_INVALID_STATE, "pending failed-init cleanup fences init");
        CHECK(esp_video_deinit() == ESP_OK, "failed-init cleanup can finish");
        CHECK(all_released(), "failed-init cleanup must release every resource");
        CHECK(esp_video_init(&config) == ESP_OK, "next complete lifecycle initializes afresh");
        CHECK(esp_video_deinit() == ESP_OK && all_released(), "next complete lifecycle cleans up");
        return 0;
    }
    const bool unrelated = strcmp(scenario, "unrelated_flags") == 0;
    const uint32_t flags = unrelated ? ESP_VIDEO_INIT_FLAGS_ALL : ESP_VIDEO_INIT_FLAGS_DVP;
    CHECK(esp_video_init_with_flags(&config, flags) == ESP_OK, "initialization succeeds");
    if (strcmp(scenario, "normal") == 0) {
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            if (cycle != 0)
                CHECK(esp_video_init_with_flags(&config, flags) == ESP_OK, "second initialization");
            CHECK(esp_video_deinit() == ESP_OK && all_released(), "complete lifecycle cleanup");
            const unsigned lookups = calls[LOOKUP];
            CHECK(esp_video_deinit() == ESP_OK && calls[LOOKUP] == lookups,
                  "completed cleanup is idempotent");
        }
        CHECK(initializations == 2, "two independent lifecycle initializations");
        return 0;
    }
    enum operation operation = PORT;
    if (strcmp(scenario, "video_failure") == 0) operation = VIDEO;
    if (strcmp(scenario, "sccb_failure") == 0 || unrelated) operation = SCCB;
    if (strcmp(scenario, "sensor_failure") == 0 ||
        strcmp(scenario, "retry_after_sensor_failure") == 0) operation = SENSOR;
    if (strcmp(scenario, "lookup_failure") == 0) operation = LOOKUP;
    esp_cam_sensor_device_t *sensor = published_sensor;
    esp_sccb_io_handle_t sccb = sensor->sccb_handle;
    inject(operation);
    CHECK(esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP) != ESP_OK,
          "injected cleanup failure must propagate");
    CHECK(failures_remaining == 0, "fault injection was consumed");
    const unsigned lookups = calls[LOOKUP], videos = calls[VIDEO], sccbs = calls[SCCB];
    if (operation == VIDEO)
        CHECK(video_live && live_pointer(sensor, true) && live_pointer(sccb, false),
              "VFS failure must preserve published sensor and SCCB");
    if (strncmp(scenario, "retry_after_", 12) != 0 && operation != LOOKUP) {
        const unsigned inits = initializations;
        const uint32_t retained_flags = s_video_device_inited_flags;
        CHECK(esp_video_init(&config) == ESP_ERR_INVALID_STATE, "pending cleanup must reject DVP init");
        CHECK(initializations == inits && s_video_device_inited_flags == retained_flags,
              "rejected init must leave existing flags and ownership unchanged");
    }
    if (unrelated) {
        CHECK(jpeg_live && (s_video_device_inited_flags & ESP_VIDEO_INIT_FLAGS_JPEG_ENC),
              "DVP failure does not clean unrelated JPEG");
        CHECK(esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_JPEG_ENC) == ESP_OK,
              "unrelated existing JPEG remains usable");
        const esp_video_init_config_t jpeg_only = { 0 };
        const unsigned before = initializations;
        const uint32_t retained_flags = s_video_device_inited_flags;
        CHECK(esp_video_init(&jpeg_only) == ESP_OK,
              "ALL flags without DVP config must not be blocked by DVP cleanup");
        CHECK(initializations == before && s_video_device_inited_flags == retained_flags,
              "JPEG-only ALL flags must not reinitialize or discard pending DVP");
    }
    CHECK(esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP) == ESP_OK,
          "retry must not reuse released handles");
    CHECK(invalid_accesses == 0, "no access to retired sensor SCCB or video");
    if (operation != LOOKUP) CHECK(calls[LOOKUP] == lookups, "retry must not look up removed video");
    if (operation != LOOKUP && operation != VIDEO)
        CHECK(calls[VIDEO] == videos, "retry must not destroy removed video twice");
    if (operation == SENSOR || operation == PORT)
        CHECK(calls[SCCB] == sccbs, "retry must not destroy removed SCCB twice");
    CHECK(esp_video_deinit() == ESP_OK && all_released(), "every owned resource is released");
    CHECK(esp_video_init_with_flags(&config, flags) == ESP_OK,
          "subsequent lifecycle initializes new state");
    CHECK(esp_video_deinit() == ESP_OK && all_released(), "subsequent lifecycle cleans up");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    scenario = argv[1];
    const int result = run();
    for (size_t i = 0; i < allocation_count; ++i) free(allocations[i].pointer);
    if (result == 0) printf("PASS %s\n", scenario);
    return result;
}

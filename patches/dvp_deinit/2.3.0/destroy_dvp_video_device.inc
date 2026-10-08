/* The existing s_init_lock owns this state, including init admission. */
typedef struct {
    esp_cam_sensor_device_t *sensor;
    bool pending;
    bool video_removed;
    bool sccb_removed;
    bool sensor_removed;
} rodak_dvp_cleanup_state_t;

static rodak_dvp_cleanup_state_t s_rodak_dvp_cleanup;

static esp_err_t destroy_dvp_video_device(void)
{
    rodak_dvp_cleanup_state_t *cleanup = &s_rodak_dvp_cleanup;
    const int dvp_ctlr_id = 0;

    if (!cleanup->pending) {
        esp_video_cam_t cam;
        ESP_RETURN_ON_ERROR(esp_video_device_common_get_video_cam(DVP_NAME, &cam), TAG, "Failed to get DVP video device camera");
        ESP_RETURN_ON_FALSE(cam.sensor, ESP_ERR_INVALID_STATE, TAG, "DVP sensor is NULL");
        cleanup->sensor = cam.sensor;
        cleanup->pending = true;
    }

    /* VFS removal can fail while allocating its pathname. Keep the sensor/SCCB
     * alive until the published video object is gone; a failed unregister must
     * not leave an openable VFS object pointing to a freed sensor. */
    if (!cleanup->video_removed) {
        ESP_RETURN_ON_ERROR(esp_video_destroy_dvp_video_device(), TAG, "Failed to destroy DVP video device");
        cleanup->video_removed = true;
    }
    if (!cleanup->sccb_removed) {
        ESP_RETURN_ON_ERROR(esp_sccb_del_i2c_io(cleanup->sensor->sccb_handle), TAG, "Failed to delete SCCB device");
        cleanup->sccb_removed = true;
        cleanup->sensor->sccb_handle = NULL;
    }
    if (!cleanup->sensor_removed) {
        ESP_RETURN_ON_ERROR(destroy_cam_device(cleanup->sensor), TAG, "Failed to delete DVP sensor");
        cleanup->sensor_removed = true;
        cleanup->sensor = NULL;
    }
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_dvp_deinit(dvp_ctlr_id), TAG, "Failed to deinit DVP port");

    /* Only a complete cleanup releases the context for the next lifecycle. */
    memset(cleanup, 0, sizeof(*cleanup));
    return ESP_OK;
}

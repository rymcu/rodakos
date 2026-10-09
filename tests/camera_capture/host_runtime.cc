#include "host_runtime.h"
#include "../task_retirement/task_retirement_host.h"
#include "phone_os/task-retirement.h"
#include "dev_fs_fat.h"
#include "esp_board_manager.h"
#include "esp_jpeg_enc.h"
#include "esp_video_ioctl.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr int kCameraFd = 30000;
std::mutex mappings_mutex;
std::set<void*> mappings;
bool IsPreviewTask() {
    return std::strcmp(retirement_host::CurrentTaskName(), "camera_preview") == 0;
}
constexpr size_t kPreviewFrameBytes = 2 * 2 * 2;
std::mutex preview_allocations_mutex;
std::array<void*, 32> preview_allocations{};
std::mutex failure_mutex;
size_t failure_bytes = 0, failure_nth = 0, failure_count = 0;
camera_host::AllocationThread failure_thread = camera_host::AllocationThread::kCaller;
std::string mount_path;
sdmmc_card_t card;
dev_fs_fat_handle_t board_handle{&card, nullptr};

void ForgetPreviewAllocation(void* pointer) {
    std::lock_guard<std::mutex> lock(preview_allocations_mutex);
    for (auto& allocation : preview_allocations) {
        if (allocation == pointer && pointer != nullptr) {
            allocation = nullptr;
            camera_host::preview_frame_bytes -= kPreviewFrameBytes;
            break;
        }
    }
}

bool RejectNew(size_t bytes) {
    std::lock_guard<std::mutex> lock(failure_mutex);
    const auto thread = !retirement_host::IsWorkerTask() ? camera_host::AllocationThread::kCaller
        : IsPreviewTask() ? camera_host::AllocationThread::kPreview
                          : camera_host::AllocationThread::kJpeg;
    if (failure_count == 0 || (failure_bytes != SIZE_MAX && bytes != failure_bytes) ||
        thread != failure_thread) return false;
    if (failure_nth > 1) { --failure_nth; return false; }
    --failure_count;
    ++camera_host::new_failures;
    return true;
}
}

namespace camera_host {
std::atomic<bool> fail_mount{false}, fail_directory{false}, fail_write{false}, fail_flush{false}, fail_close{false};
std::atomic<bool> fail_encoder_open{false}, fail_encoder_process{false}, fail_allocation{false}, empty_encoded{false};
std::atomic<bool> collide_on_create{false};
std::atomic<bool> fail_dequeue{false};
std::atomic<bool> pause_frames{false}, frames_paused{false};
std::atomic<unsigned> encoder_handles{0}, frame_mappings{0};
std::atomic<size_t> preview_frame_bytes{0};
std::atomic<unsigned> new_failures{0}, aligned_buffers{0}, dequeued_buffers{0}, requeued_buffers{0};
std::function<void()> write_hook;
std::function<void()> preview_state_query_hook;
std::function<void()> streamoff_hook;
std::function<void(const char*)> log_hook;
std::atomic<int> streamon_result{0}, streamoff_result{0};
std::atomic<unsigned> streamon_calls{0}, streamoff_calls{0};
std::atomic<unsigned> test_pattern_calls{0};
std::atomic<int> test_pattern_value{0};
std::atomic<unsigned> sensor_register_reads{0}, sensor_register_writes{0};
std::atomic<unsigned> sensor_register_page{0};
std::array<std::array<uint8_t, 256>, 2> sensor_registers{};
void ObserveLog(const char* format) {
    if (log_hook) log_hook(format);
}
std::string collision_path;
void Reset(const std::string& path) {
    ClearNewFailures();
    retirement_host::Reset();
    retirement_host::SetAutoStart(true);
    new_failures = dequeued_buffers = requeued_buffers = 0;
    streamon_calls = streamoff_calls = 0;
    test_pattern_calls = 0;
    test_pattern_value = 0;
    sensor_register_reads = sensor_register_writes = 0;
    sensor_register_page = 0;
    for (size_t page = 0; page < sensor_registers.size(); ++page) {
        for (size_t address = 0; address < sensor_registers[page].size(); ++address) {
            sensor_registers[page][address] = static_cast<uint8_t>((page << 7) ^ address);
        }
    }
    mount_path = path;
    board_handle.mount_point = mount_path.c_str();
    fail_mount = fail_directory = fail_write = fail_flush = fail_close = false;
    fail_encoder_open = fail_encoder_process = fail_allocation = empty_encoded = false;
    collide_on_create = false;
    fail_dequeue = false;
    pause_frames = frames_paused = false;
    write_hook = {};
    preview_state_query_hook = {};
    streamoff_hook = {};
    log_hook = {};
    streamon_result = streamoff_result = 0;
    collision_path.clear();
}
void FailNew(size_t bytes, size_t nth, size_t count, AllocationThread thread) {
    std::lock_guard<std::mutex> lock(failure_mutex);
    failure_bytes = bytes;
    failure_nth = nth;
    failure_count = count;
    failure_thread = thread;
}
void ClearNewFailures() {
    std::lock_guard<std::mutex> lock(failure_mutex);
    failure_count = 0;
}
void* AllocateAligned(size_t alignment, size_t bytes) {
    if (fail_allocation) return nullptr;
    void* pointer = nullptr;
    if (posix_memalign(&pointer, alignment, bytes) != 0) return nullptr;
    ++aligned_buffers;
    return pointer;
}
void FreeAligned(void* pointer) {
    if (pointer != nullptr) { --aligned_buffers; std::free(pointer); }
}
bool IsCameraConfigured() {
    if (IsPreviewTask() && preview_state_query_hook) preview_state_query_hook();
    return true;
}
void JoinTasks() {
    // Test cleanup uses the same permanent-owner pump as the application.
    rodakos::PumpTaskRetirements();
    retirement_host::JoinTasks();
}
std::vector<uint8_t> EncodedBytes() { return {0xff, 0xd8, 1, 2, 3, 4, 0xff, 0xd9}; }
}

esp_err_t esp_board_manager_init_device_by_name(const char*) { return camera_host::fail_mount ? -1 : ESP_OK; }
esp_err_t esp_board_manager_deinit_device_by_name(const char*) { return ESP_OK; }
esp_err_t esp_board_manager_get_device_handle(const char*, void** handle) { *handle = &board_handle; return ESP_OK; }

int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

jpeg_error_t jpeg_enc_open(const jpeg_enc_config_t* config, jpeg_enc_handle_t* encoder) {
    if (camera_host::fail_encoder_open || config->width != 2 || config->height != 2) return -1;
    *encoder = new int(1);
    ++camera_host::encoder_handles;
    return JPEG_ERR_OK;
}
jpeg_error_t jpeg_enc_process(jpeg_enc_handle_t, const uint8_t*, int input_bytes,
                             uint8_t* output, int capacity, int* output_bytes) {
    if (camera_host::fail_encoder_process || input_bytes != 12) return -1;
    const auto bytes = camera_host::EncodedBytes();
    if (capacity < static_cast<int>(bytes.size())) return -1;
    std::copy(bytes.begin(), bytes.end(), output);
    *output_bytes = camera_host::empty_encoded ? 0 : static_cast<int>(bytes.size());
    return JPEG_ERR_OK;
}
void jpeg_enc_close(jpeg_enc_handle_t encoder) { delete static_cast<int*>(encoder); --camera_host::encoder_handles; }

extern "C" {
void* __real__Znwm(size_t);
void __real__ZdlPv(void*);
void __real__ZdlPvm(void*, size_t);

void* __wrap__Znwm(size_t size) {
    if (RejectNew(size)) throw std::bad_alloc();
    void* pointer = __real__Znwm(size);
    // Track the fake device's RGB565 allocations made by the production preview
    // worker, including buffers retained by the service after the worker exits.
    if (IsPreviewTask() && size == kPreviewFrameBytes) {
        std::lock_guard<std::mutex> lock(preview_allocations_mutex);
        const auto free_slot = std::find(preview_allocations.begin(), preview_allocations.end(), nullptr);
        if (free_slot == preview_allocations.end()) std::abort();
        *free_slot = pointer;
        camera_host::preview_frame_bytes += size;
    }
    return pointer;
}
void __wrap__ZdlPv(void* pointer) {
    ForgetPreviewAllocation(pointer);
    __real__ZdlPv(pointer);
}
void __wrap__ZdlPvm(void* pointer, size_t size) {
    ForgetPreviewAllocation(pointer);
    __real__ZdlPvm(pointer, size);
}

int __real_open(const char*, int, ...);
int __real_close(int);
int __real_ioctl(int, unsigned long, ...);
void* __real_mmap(void*, size_t, int, int, int, off_t);
int __real_munmap(void*, size_t);
int __real_mkdir(const char*, mode_t);
size_t __real_fwrite(const void*, size_t, size_t, FILE*);
int __real_fflush(FILE*);
int __real_fclose(FILE*);

int __wrap_open(const char* path, int flags, ...) {
    if (std::strcmp(path, "/dev/rodakos-test-camera") == 0) return kCameraFd;
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args);
    }
    if ((flags & O_EXCL) != 0 && camera_host::collide_on_create.exchange(false)) {
        const int competitor = __real_open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (competitor >= 0) {
            const char content[] = "existing-photo";
            (void)write(competitor, content, sizeof(content) - 1);
            __real_close(competitor);
            camera_host::collision_path = path;
        }
    }
    return (flags & O_CREAT) ? __real_open(path, flags, mode) : __real_open(path, flags);
}
int __wrap_close(int fd) { return fd == kCameraFd ? 0 : __real_close(fd); }
int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list args; va_start(args, request); void* argument = va_arg(args, void*); va_end(args);
    if (fd != kCameraFd) return __real_ioctl(fd, request, argument);
    if (request == VIDIOC_STREAMON) {
        ++camera_host::streamon_calls;
        const int result = camera_host::streamon_result.load();
        if (result != 0) errno = ENOMEM;
        return result;
    } else if (request == VIDIOC_STREAMOFF) {
        ++camera_host::streamoff_calls;
        if (camera_host::streamoff_hook) camera_host::streamoff_hook();
        const int result = camera_host::streamoff_result.load();
        if (result != 0) errno = EBUSY;
        return result;
    } else if (request == VIDIOC_S_EXT_CTRLS || request == VIDIOC_G_EXT_CTRLS) {
        auto* controls = static_cast<v4l2_ext_controls*>(argument);
        if (controls == nullptr || controls->count != 1 || controls->controls == nullptr) {
            errno = EINVAL;
            return -1;
        }
        auto& control = controls->controls[0];
        if (controls->ctrl_class == V4L2_CTRL_CLASS_ESP_CAM_IOCTL) {
            if (control.p_u8 == nullptr || control.size != sizeof(esp_cam_sensor_reg_val_t)) {
                errno = EINVAL;
                return -1;
            }
            auto* sensor_register = reinterpret_cast<esp_cam_sensor_reg_val_t*>(control.p_u8);
            if (request == VIDIOC_S_EXT_CTRLS && control.id == ESP_CAM_SENSOR_IOC_S_REG) {
                ++camera_host::sensor_register_writes;
                if (sensor_register->regaddr == 0xfe) {
                    camera_host::sensor_register_page = sensor_register->value & 1;
                } else {
                    camera_host::sensor_registers[camera_host::sensor_register_page.load()]
                                                 [sensor_register->regaddr & 0xff] =
                        static_cast<uint8_t>(sensor_register->value);
                }
                return 0;
            }
            if (request == VIDIOC_G_EXT_CTRLS && control.id == ESP_CAM_SENSOR_IOC_G_REG) {
                ++camera_host::sensor_register_reads;
                sensor_register->value = camera_host::sensor_registers[camera_host::sensor_register_page.load()]
                    [sensor_register->regaddr & 0xff];
                return 0;
            }
            errno = EINVAL;
            return -1;
        }
        if (request != VIDIOC_S_EXT_CTRLS || control.id != V4L2_CID_TEST_PATTERN) {
            errno = EINVAL;
            return -1;
        }
        ++camera_host::test_pattern_calls;
        camera_host::test_pattern_value = control.value;
        return 0;
    } else if (request == VIDIOC_QUERYCAP) {
        static_cast<v4l2_capability*>(argument)->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    } else if (request == VIDIOC_S_FMT) {
        auto* format = static_cast<v4l2_format*>(argument);
        format->fmt.pix.width = format->fmt.pix.height = 2;
        format->fmt.pix.bytesperline = 4;
    } else if (request == VIDIOC_QUERYBUF) {
        auto* buffer = static_cast<v4l2_buffer*>(argument);
        buffer->length = 8;
        buffer->m.offset = buffer->index * 4096;
    } else if (request == VIDIOC_DQBUF) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (camera_host::fail_dequeue) { errno = EIO; return -1; }
        auto* buffer = static_cast<v4l2_buffer*>(argument);
        buffer->index = 0; buffer->bytesused = 8; buffer->flags = V4L2_BUF_FLAG_DONE;
        if (camera_host::pause_frames) { buffer->flags = 0; camera_host::frames_paused = true; }
        ++camera_host::dequeued_buffers;
    } else if (request == VIDIOC_QBUF) {
        ++camera_host::requeued_buffers;
    }
    return 0;
}
void* __wrap_mmap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    if (fd != kCameraFd) return __real_mmap(address, size, protection, flags, fd, offset);
    void* buffer = std::malloc(size);
    if (buffer == nullptr) return MAP_FAILED;
    std::memset(buffer, 0xff, size);
    { std::lock_guard<std::mutex> lock(mappings_mutex); mappings.insert(buffer); }
    ++camera_host::frame_mappings;
    return buffer;
}
int __wrap_munmap(void* address, size_t size) {
    std::lock_guard<std::mutex> lock(mappings_mutex);
    if (mappings.erase(address)) { std::free(address); --camera_host::frame_mappings; return 0; }
    return __real_munmap(address, size);
}
time_t __wrap_time(time_t* result) {
    constexpr time_t frozen = 1800000000;
    if (result) *result = frozen;
    return frozen;
}
int __wrap_mkdir(const char* path, mode_t mode) {
    if (camera_host::fail_directory.exchange(false)) { errno = EACCES; return -1; }
    return __real_mkdir(path, mode);
}
size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* file) {
    if (camera_host::write_hook) camera_host::write_hook();
    if (camera_host::fail_write.exchange(false)) {
        const size_t written = count == 0 ? 0 : __real_fwrite(data, size, count / 2, file);
        errno = ENOSPC;
        return written;
    }
    return __real_fwrite(data, size, count, file);
}
int __wrap_fflush(FILE* file) {
    const int result = __real_fflush(file);
    if (camera_host::fail_flush.exchange(false)) { errno = ENOSPC; return EOF; }
    return result;
}
int __wrap_fclose(FILE* file) {
    const int result = __real_fclose(file);
    if (camera_host::fail_close.exchange(false)) { errno = EIO; return EOF; }
    return result;
}
}

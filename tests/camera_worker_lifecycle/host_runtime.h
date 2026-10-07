#pragma once
#include <string>
#include <vector>

#include "worker_fakes.h"

namespace worker_host {
void Reset();
void Join();
void BlockLog();
bool WaitLog();
void ReleaseLog();
void QueueEvent(esp_cam_ctlr_handle_t, int);
bool WaitOwnerProgress();
bool DeletedWhileLogHeld();
size_t DeleteCalls();
size_t WithCapsDeleteCalls();
size_t LiveAllocations();
size_t LiveQueues();
void FailAllocation(int);
void FailTaskCreation(bool);
void FailTaskStack(bool);
void FailTaskTcb(bool);
void FailQueueCreation(bool);
unsigned CreatedStackCaps();
size_t CreatedStackBytes();
unsigned CreatedPriority();
void SetCurrentTask(TaskHandle_t);
void BlockReceive();
bool WaitReceive();
void ReleaseReceive();
void BlockAfterReceive();
bool WaitAfterReceive();
void ReleaseAfterReceive();
void BlockCallback();
bool WaitCallback();
void ReleaseCallback();
bool Callback(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t*, void*);
bool DeletedDuringCallback();
void DeleteFromCallback(bool);
int CallbackDeleteResult();
size_t CallbackCalls();
void BlockFinalUnlock();
bool WaitFinalUnlock();
void ReleaseFinalUnlock();
bool DeletedBeforeFinalUnlock();
bool WaitOwnerAtFinalUnlock();
void RunBeforeHandlePublished(bool);
void ProvideFrame(bool);
void BlockCaptureStart();
bool WaitCaptureStart();
void ReleaseCaptureStart();
bool DeletedDuringCaptureStart();
size_t QueueFullWakeups();
bool WorkerIsQuiesced(esp_cam_ctlr_handle_t);
std::vector<std::string> Calls();
}  // namespace worker_host

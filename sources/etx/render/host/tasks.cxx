#include <etx/core/handle.hxx>
#include <etx/core/profiler.hxx>
#include <etx/render/host/pool.hxx>
#include <etx/render/host/tasks.hxx>

#include <TaskScheduler.hxx>

#define ETX_ALWAYS_SINGLE_THREAD 0
#define ETX_DEBUG_SINGLE_THREAD  1

#if (ETX_DEBUG || ETX_ALWAYS_SINGLE_THREAD)
# define ETX_SINGLE_THREAD ETX_DEBUG_SINGLE_THREAD
#else
# define ETX_SINGLE_THREAD 0
#endif

namespace etx {

struct TaskWrapper : public enki::ITaskSet {
  Task* task = nullptr;
  bool executed = false;

  TaskWrapper(Task* t, uint32_t range, uint32_t min_size)
    : enki::ITaskSet(range, min_size)
    , task(t) {
  }

  void ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) override {
    executed = true;
    task->execute_range(range_.start, range_.end, threadnum_);
  }
};

struct FunctionTask : public Task {
  using F = std::function<void(uint32_t, uint32_t, uint32_t)>;
  F func;

  FunctionTask(F f)
    : func(f) {
  }

  void execute_range(uint32_t begin, uint32_t end, uint32_t thread_id) override {
    func(begin, end, thread_id);
  }
};

struct TaskSchedulerImpl {
  enki::TaskScheduler scheduler;
  ObjectIndexPool<TaskWrapper> task_pool;
  ObjectIndexPool<FunctionTask> function_task_pool;
  std::map<uint32_t, uint32_t> task_to_function;

  TaskSchedulerImpl() {
    task_pool.init(1024u);
    function_task_pool.init(1024u);

    enki::TaskSchedulerConfig config = {};
    config.numExternalTaskThreads = 1u;
    config.numTaskThreadsToCreate = ETX_SINGLE_THREAD ? 1 : (enki::GetNumHardwareThreads() + 1u + config.numExternalTaskThreads);
    config.profilerCallbacks.threadStart = [](uint32_t thread_id) {
      ETX_PROFILER_REGISTER_THREAD;
    };
    scheduler.Initialize(config);
  }

  ~TaskSchedulerImpl() {
    ETX_ASSERT(task_pool.alive_objects_count() == 0);
    task_pool.cleanup();
  }
};

TaskScheduler::TaskScheduler() {
  ETX_PIMPL_INIT(TaskScheduler);
}

TaskScheduler::~TaskScheduler() {
  ETX_PIMPL_CLEANUP(TaskScheduler);
}

uint32_t TaskScheduler::max_thread_count() {
  return _private->scheduler.GetConfig().numTaskThreadsToCreate + 2u;
}

void TaskScheduler::register_thread() {
  _private->scheduler.RegisterExternalTaskThread();
}

Task::Handle TaskScheduler::schedule(uint32_t range, Task* t) {
  auto handle = _private->task_pool.alloc(t, range, 1u);
  auto& task_wrapper = _private->task_pool.get(handle);
  _private->scheduler.AddTaskSetToPipe(&task_wrapper);
  return {handle};
}

Task::Handle TaskScheduler::schedule(uint32_t range, std::function<void(uint32_t, uint32_t, uint32_t)> func) {
  auto func_task_handle = _private->function_task_pool.alloc(func);
  auto& func_task = _private->function_task_pool.get(func_task_handle);

  auto task_handle = _private->task_pool.alloc(&func_task, range, 1u);
  auto& task = _private->task_pool.get(task_handle);

  _private->task_to_function[task_handle] = func_task_handle;
  _private->scheduler.AddTaskSetToPipe(&task);

  return {task_handle};
}

void TaskScheduler::execute(uint32_t range, Task* t) {
  auto handle = schedule(range, t);
  wait(handle);
}

void TaskScheduler::execute(uint32_t range, std::function<void(uint32_t, uint32_t, uint32_t)> func) {
  auto handle = schedule(range, func);
  wait(handle);
}

void TaskScheduler::execute_linear(uint32_t range, std::function<void(uint32_t, uint32_t, uint32_t)> func) {
  func(0u, range, 0u);
}

bool TaskScheduler::completed(Task::Handle handle) {
  if (handle.data == Task::InvalidHandle) {
    return true;
  }

  auto& task_wrapper = _private->task_pool.get(handle.data);
  return task_wrapper.executed && task_wrapper.GetIsComplete();
}

void TaskScheduler::wait(Task::Handle& handle) {
  if (handle.data == Task::InvalidHandle) {
    return;
  }

  auto& task_wrapper = _private->task_pool.get(handle.data);
  _private->scheduler.WaitforTask(&task_wrapper);
  _private->task_pool.free(handle.data);

  auto func_task = _private->task_to_function.find(handle.data);
  if (func_task != _private->task_to_function.end()) {
    _private->function_task_pool.free(func_task->second);
    _private->task_to_function.erase(func_task);
  }

  handle.data = Task::InvalidHandle;
}

void TaskScheduler::restart(Task::Handle handle) {
  if (handle.data == Task::InvalidHandle) {
    return;
  }

  auto& task_wrapper = _private->task_pool.get(handle.data);
  _private->scheduler.WaitforTask(&task_wrapper);
  _private->scheduler.AddTaskSetToPipe(&task_wrapper);
}

}  // namespace etx

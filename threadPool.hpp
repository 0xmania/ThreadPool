//使用注意  不可使用 __fastcall  
#ifndef _THREADPOOL_HPP_
#define _THREADPOOL_HPP_
#include<thread>
#include<mutex>
#include<deque>
#include<future>
#include<vector>
#include<atomic>
#include<condition_variable>

class CThreadPool
{

private:
	bool                        m_isWork;
	unsigned short              m_defaulThreadNum;     //初始化线程数量
	unsigned short              m_maxThreadNum;        //最大化线程数量
	std::atomic<unsigned short> m_nowThreadNum;        //现有线程数量
	std::atomic<unsigned short> m_idleThreadNum;       //空闲线程数量
	std::vector<std::thread>    m_threads;             //线程队列

	std::deque<std::function<void()> >  m_taskQueue;   //任务队列
	std::condition_variable     m_taskCV;              //任务队列环境变量触发
	std::mutex                  m_taskMutex;           //任务队列环境变量相关

	//使用互斥锁 更稳定 原子自旋会莫名其妙崩溃
	std::mutex                  m_taskQueueMutex;     //用于保护任务队列 
	std::mutex                  m_threadMutex;        //用于保护增加和释放线程
public:
	//构造函数 默认线程数 最大扩展线程数 
	CThreadPool(unsigned short defualThreadNum = 6, unsigned short maxThreadNum = 12) {
		m_isWork = true;
		m_defaulThreadNum = defualThreadNum;
		m_maxThreadNum = maxThreadNum;
		for (unsigned short i = 0; i < defualThreadNum; ++i) addThread();
	}
	~CThreadPool() {
		m_isWork = false;
		m_taskCV.notify_all();
		for (auto i = m_threads.begin(); i != m_threads.end(); ++i) {
			if ((*i).joinable()) (*i).join();
		}
	}
	//不允许复制
	CThreadPool(const CThreadPool&) = delete;
	CThreadPool& operator=(const CThreadPool&) = delete;
	template <typename F, typename... Args>
	auto submit(F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		using Ret = decltype(f(args...));                                                                  //获取返回结果类型
		std::function<Ret()> taskFunction = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //绑定函数
		// 使用std::packaged_task打包成一个shared_ptr指针对象，用于异步执行、返回结果
		auto taskPtr = std::make_shared< std::packaged_task<Ret()> >(taskFunction);

		// 进一步将上述结果绑定成一个std::function<void()>函数， 任务队列中元素类型   
		std::function<void()> task = [taskPtr]() {
			(*taskPtr)();
		};
		// 放入任务队列

		m_taskQueueMutex.lock();
		m_taskQueue.push_back(task);
		m_taskQueueMutex.unlock();
		//如果无空闲线程切线程数未到最大 那么添加一个线程并执行
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum) addThread();
		// 唤醒一个线程， 异步执行当前任务 
		m_taskCV.notify_one();
		// 返回当前任务的std::future指针， 可以使用std::future的get()函数等待任务执行结果   
		return taskPtr->get_future();
	}
	template <typename F, typename... Args>
	auto preSubmit(F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		using Ret = decltype(f(args...));                                                                  //获取返回结果类型
		std::function<Ret()> taskFunction = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //绑定函数
		// 使用std::packaged_task打包成一个shared_ptr指针对象，用于异步执行、返回结果
		auto taskPtr = std::make_shared< std::packaged_task<Ret()> >(taskFunction);

		// 进一步将上述结果绑定成一个std::function<void()>函数， 任务队列中元素类型   
		std::function<void()> task = [taskPtr]() {
			(*taskPtr)();
		};
		// 放入任务队列

		m_taskQueueMutex.lock();
		m_taskQueue.push_front(task);
		m_taskQueueMutex.unlock();
		//如果无空闲线程切线程数未到最大 那么添加一个线程并执行
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum) addThread(); 
		// 唤醒一个线程， 异步执行当前任务 
		m_taskCV.notify_one();
		// 返回当前任务的std::future指针， 可以使用std::future的get()函数等待任务执行结果   
		return taskPtr->get_future();
	}
	template <typename F, typename... Args>
	void submitNoReturn(F&& f, Args&& ...args) {
		std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //绑定函数
		m_taskQueueMutex.lock();
		m_taskQueue.push_back(task);
		m_taskQueueMutex.unlock();
		//如果无空闲线程切线程数未到最大 那么添加一个线程并执行
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum)  addThread();
		// 唤醒一个线程， 异步执行当前任务 
		m_taskCV.notify_one();
	}
	template <typename F, typename... Args>
	void preSubmitNoReturn(F&& f, Args&& ...args) {
		std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //绑定函数
		m_taskQueueMutex.lock();
		m_taskQueue.push_front(task);
		m_taskQueueMutex.unlock();
		//如果无空闲线程切线程数未到最大 那么添加一个线程并执行
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum) addThread(); 
		// 唤醒一个线程， 异步执行当前任务 
		m_taskCV.notify_one();
	}
private:
	void addThread() {
		//确保线程安全

		m_threadMutex.lock();
		m_threads.emplace_back([this] {

			std::function<void()> task;
		//必须处理完全部的任务后退出
		while (true) {
			{
				std::unique_lock<std::mutex> lock{ m_taskMutex };
				m_taskCV.wait(lock, [this] {
					return !m_taskQueue.empty() || !m_isWork;
					});
			}
			//判断是否线程池终止
			if (!m_isWork && m_taskQueue.empty()) return;
			m_taskQueueMutex.lock();
			if (m_taskQueue.empty()) { m_taskQueueMutex.unlock(); continue; }
			task = std::move(m_taskQueue.front());
			m_taskQueue.pop_front();
			m_taskQueueMutex.unlock();
			--m_idleThreadNum;

			//执行任务
			task();
			++m_idleThreadNum;
			//如果线程过多那么释放一部分
			if (m_idleThreadNum >= m_defaulThreadNum) {
				--m_idleThreadNum;
				--m_nowThreadNum;
				this->submitNoReturn(std::bind(&CThreadPool::releaseThread, this, std::this_thread::get_id()));
				return;
			}
		}
			});
		m_threadMutex.unlock();
		//线程增加
		++m_nowThreadNum;
		++m_idleThreadNum;
	}
	void releaseThread(std::thread::id threadId) {
		for (auto i = m_threads.begin(); i != m_threads.end(); ++i) {
			if (threadId == (*i).get_id()) {
				if ((*i).joinable()) (*i).join();
				m_threadMutex.lock();
				m_threads.erase(i);
				m_threadMutex.unlock();
				return;
			}

		}
	}

};

#endif // !_THREADPOOL_HPP_

//ʹ��ע��  ����ʹ�� __fastcall  
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
	unsigned short              m_defaulThreadNum;     //��ʼ���߳�����
	unsigned short              m_maxThreadNum;        //����߳�����
	std::atomic<unsigned short> m_nowThreadNum;        //�����߳�����
	std::atomic<unsigned short> m_idleThreadNum;       //�����߳�����
	std::vector<std::thread>    m_threads;             //�̶߳���

	std::deque<std::function<void()> >  m_taskQueue;   //�������
	std::condition_variable     m_taskCV;              //������л�����������
	std::mutex                  m_taskMutex;           //������л����������

	//ʹ�û����� ���ȶ� ԭ��������Ī���������
	std::mutex                  m_taskQueueMutex;     //���ڱ���������� 
	std::mutex                  m_threadMutex;        //���ڱ������Ӻ��ͷ��߳�
public:
	//���캯�� Ĭ���߳��� �����չ�߳��� 
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
	//��������
	CThreadPool(const CThreadPool&) = delete;
	CThreadPool& operator=(const CThreadPool&) = delete;
	template <typename F, typename... Args>
	auto submit(F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		using Ret = decltype(f(args...));                                                                  //��ȡ���ؽ������
		std::function<Ret()> taskFunction = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //�󶨺���
		// ʹ��std::packaged_task�����һ��shared_ptrָ����������첽ִ�С����ؽ��
		auto taskPtr = std::make_shared< std::packaged_task<Ret()> >(taskFunction);

		// ��һ������������󶨳�һ��std::function<void()>������ ���������Ԫ������   
		std::function<void()> task = [taskPtr]() {
			(*taskPtr)();
		};
		// �����������

		m_taskQueueMutex.lock();
		m_taskQueue.push_back(task);
		m_taskQueueMutex.unlock();
		//����޿����߳����߳���δ����� ��ô���һ���̲߳�ִ��
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum) addThread();
		// ����һ���̣߳� �첽ִ�е�ǰ���� 
		m_taskCV.notify_one();
		// ���ص�ǰ�����std::futureָ�룬 ����ʹ��std::future��get()�����ȴ�����ִ�н��   
		return taskPtr->get_future();
	}
	template <typename F, typename... Args>
	auto preSubmit(F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		using Ret = decltype(f(args...));                                                                  //��ȡ���ؽ������
		std::function<Ret()> taskFunction = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //�󶨺���
		// ʹ��std::packaged_task�����һ��shared_ptrָ����������첽ִ�С����ؽ��
		auto taskPtr = std::make_shared< std::packaged_task<Ret()> >(taskFunction);

		// ��һ������������󶨳�һ��std::function<void()>������ ���������Ԫ������   
		std::function<void()> task = [taskPtr]() {
			(*taskPtr)();
		};
		// �����������

		m_taskQueueMutex.lock();
		m_taskQueue.push_front(task);
		m_taskQueueMutex.unlock();
		//����޿����߳����߳���δ����� ��ô���һ���̲߳�ִ��
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum) addThread(); 
		// ����һ���̣߳� �첽ִ�е�ǰ���� 
		m_taskCV.notify_one();
		// ���ص�ǰ�����std::futureָ�룬 ����ʹ��std::future��get()�����ȴ�����ִ�н��   
		return taskPtr->get_future();
	}
	template <typename F, typename... Args>
	void submitNoReturn(F&& f, Args&& ...args) {
		std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //�󶨺���
		m_taskQueueMutex.lock();
		m_taskQueue.push_back(task);
		m_taskQueueMutex.unlock();
		//����޿����߳����߳���δ����� ��ô���һ���̲߳�ִ��
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum)  addThread();
		// ����һ���̣߳� �첽ִ�е�ǰ���� 
		m_taskCV.notify_one();
	}
	template <typename F, typename... Args>
	void preSubmitNoReturn(F&& f, Args&& ...args) {
		std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //�󶨺���
		m_taskQueueMutex.lock();
		m_taskQueue.push_front(task);
		m_taskQueueMutex.unlock();
		//����޿����߳����߳���δ����� ��ô���һ���̲߳�ִ��
		if (m_idleThreadNum == 0 && m_nowThreadNum < m_maxThreadNum) addThread(); 
		// ����һ���̣߳� �첽ִ�е�ǰ���� 
		m_taskCV.notify_one();
	}
private:
	void addThread() {
		//ȷ���̰߳�ȫ

		m_threadMutex.lock();
		m_threads.emplace_back([this] {

			std::function<void()> task;
		//���봦����ȫ����������˳�
		while (true) {
			{
				std::unique_lock<std::mutex> lock{ m_taskMutex };
				m_taskCV.wait(lock, [this] {
					return !m_taskQueue.empty() || !m_isWork;
					});
			}
			//�ж��Ƿ��̳߳���ֹ
			if (!m_isWork && m_taskQueue.empty()) return;
			m_taskQueueMutex.lock();
			if (m_taskQueue.empty()) { m_taskQueueMutex.unlock(); continue; }
			task = std::move(m_taskQueue.front());
			m_taskQueue.pop_front();
			m_taskQueueMutex.unlock();
			--m_idleThreadNum;

			//ִ������
			task();
			++m_idleThreadNum;
			//����̹߳�����ô�ͷ�һ����
			if (m_idleThreadNum >= m_defaulThreadNum) {
				--m_idleThreadNum;
				--m_nowThreadNum;
				this->submitNoReturn(std::bind(&CThreadPool::releaseThread, this, std::this_thread::get_id()));
				return;
			}
		}
			});
		m_threadMutex.unlock();
		//�߳�����
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

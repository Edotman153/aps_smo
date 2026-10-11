// Имитационная модель системы массового обслуживания (СМО).
//
//   источники (Family) -> приём (RequestAcceptanceSystem) -> буфер (RequestBuffer)
//        -> диспетчер (RequestDistributionSystem) -> приборы (Bank)
//
// Каждый компонент работает в своём потоке, время реальное (миллисекунды).
//
// Запуск:  smo [каталог_результатов] [секунд] [--quiet]
//   каталог_результатов  куда записать banks.txt и familys.txt (по умолчанию текущий)
//   секунд               длительность моделирования (по умолчанию 15)
//   --quiet, -q          не печатать ход моделирования в консоль

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------- параметры модели
constexpr size_t numFamilys = 5;   // число источников заявок
constexpr size_t bufSize = 10;     // ёмкость буфера
constexpr size_t numBanks = 7;     // число приборов
constexpr int familyStepMs = 50;   // источник создаёт заявку через 1 или 2 шага по 50 мс
constexpr double meanServiceMs = 100.0;  // обслуживание: экспоненциальное, среднее 100 мс

// ---------------------------------------------------------------- общие вещи
using clock_type = std::chrono::steady_clock;
using time_type = clock_type::time_point;

static long long elapsedMs(time_type from, time_type to)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

static std::atomic<bool> running{ true };  // сбрасывается в конце моделирования
static std::atomic<bool> quiet{ false };
static std::mutex coutMutex;
static std::mutex stopMutex;
static std::condition_variable stopCv;

static void log(const std::string& s)
{
	if (quiet)
	{
		return;
	}
	std::lock_guard<std::mutex> lk(coutMutex);
	std::cout << s << '\n';
}

// Пауза, которую можно прервать остановкой моделирования. false - модель остановлена.
static bool sleepFor(std::chrono::milliseconds d)
{
	std::unique_lock<std::mutex> lk(stopMutex);
	return !stopCv.wait_for(lk, d, [] { return !running.load(); });
}

struct stat_t
{
	size_t allRequests = 0;
	size_t proceededRequests = 0;
	size_t rejectedRequests = 0;
	std::vector< size_t > waitTime;     // время от создания заявки до отправки на прибор, мс
	std::vector< size_t > processTime;  // время от отправки на прибор до конца обслуживания, мс
};
static std::map< size_t, stat_t > stats;
static std::mutex statsMutex;

// ---------------------------------------------------------------- заявка и источник
class Family;

class Request
{
public:
	Request(const Family* f, size_t id) :
		f_(f),
		id_(id),
		createTime(clock_type::now())
	{}
	const Family* getFamily() const { return f_; }
	size_t getid() const { return id_; }
	time_type getSendTime() const { return sendTime; }
	void setSendTime(time_type t) { sendTime = t; }
	time_type getCreateTime() const { return createTime; }
private:
	const Family* f_;
	size_t id_;
	time_type createTime;
	time_type sendTime{};
};

class RequestAcceptanceSystem;

class Family
{
public:
	Family(size_t id, RequestAcceptanceSystem* acc) :
		acc_(acc),
		id_(id),
		priority_(id)  // приоритет источника равен его номеру
	{
		std::lock_guard<std::mutex> lk(statsMutex);
		stats[id_];
	}
	void generateAndSendRequest();
	size_t getPriority() const { return priority_; }
	size_t getid() const { return id_; }
private:
	RequestAcceptanceSystem* acc_;
	size_t id_;
	size_t priority_;
	inline static size_t nextRequestId = 0;
	inline static std::mutex genMutex{};
};

// ---------------------------------------------------------------- буфер
class RequestBuffer
{
public:
	explicit RequestBuffer(size_t cap) : capacity(cap) { data.reserve(cap); }
	~RequestBuffer()
	{
		for (Request* r : data)
		{
			delete r;
		}
	}
	RequestBuffer(const RequestBuffer&) = delete;
	RequestBuffer& operator=(const RequestBuffer&) = delete;

	// Кладёт заявку в буфер. Если буфер полон, вместо неё вытесняется последняя из лежащих
	// заявок: она остаётся в роли отказа и возвращается (иначе nullptr).
	Request* insertOrReplaceLast(Request* r)
	{
		std::lock_guard<std::mutex> lk(mutex);
		print();
		Request* evicted = nullptr;
		if (data.size() >= capacity)
		{
			evicted = data.back();
			data.back() = r;
		}
		else
		{
			data.push_back(r);
		}
		log("Inserted " + std::to_string(r->getid()));
		print();
		return evicted;
	}

	// Достаёт заявку на обслуживание: с наибольшим приоритетом, а при равных приоритетах
	// самую раннюю. Заявки в буфере лежат в порядке возрастания id, поэтому проверка
	// id < minid ниже срабатывает только на первой заявке (когда ещё ничего не выбрано).
	Request* extractBest()
	{
		std::lock_guard<std::mutex> lk(mutex);
		if (data.empty())
		{
			return nullptr;
		}
		size_t maxPrio = 0;
		size_t I = 0;
		size_t minid = static_cast<size_t>(-1);
		for (size_t i = 0; i < data.size(); ++i)
		{
			size_t prio = data[i]->getFamily()->getPriority();
			size_t id = data[i]->getid();
			if (prio > maxPrio)
			{
				maxPrio = prio;
				I = i;
				minid = id;
			}
			if (id < minid)
			{
				minid = id;
				I = i;
			}
		}
		Request* res = data[I];
		data.erase(data.begin() + I);
		log("Request " + std::to_string(res->getid()) + " was chosen");
		return res;
	}

private:
	std::vector< Request* > data;
	size_t capacity;
	std::mutex mutex;

	void print() // вызывать под mutex
	{
		if (quiet)
		{
			return;
		}
		std::string s = "RequestBuffer: ";
		for (size_t i = 0; i < capacity; i++)
		{
			s += (i < data.size() ? std::to_string(data[i]->getid()) : "_");
			s += ' ';
		}
		log(s);
	}
};

// ---------------------------------------------------------------- приборы
class RequestDistributionSystem;
class BanksManageSystem;

class Bank
{
public:
	Bank(BanksManageSystem* m, size_t i) : man(m), id(i) {}
	~Bank() { delete toProceed; }
	Bank(const Bank&) = delete;
	Bank& operator=(const Bank&) = delete;

	void addRequest(Request* r)
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
			toProceed = r;
		}
		cv.notify_one();
	}
	void wake()
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
		}
		cv.notify_all();
	}
	void run();
	size_t getid() const { return id; }
	// Суммарное время обслуживания, мс (читать после остановки потока)
	size_t getAvailableTime() const { return availableTime; }
private:
	BanksManageSystem* man;
	size_t id;
	Request* toProceed = nullptr;
	size_t availableTime = 0;
	std::mutex mutex;
	std::condition_variable cv;
};

class BanksManageSystem
{
public:
	static constexpr size_t none = static_cast<size_t>(-1);

	BanksManageSystem()
	{
		for (size_t i = 0; i < numBanks; i++)
		{
			banks.emplace_back(this, i);
		}
		availables.assign(numBanks, true);
	}
	// Первый свободный прибор, начиная с номера from (по кругу); он помечается занятым.
	size_t chooseBank(size_t from)
	{
		std::lock_guard<std::mutex> lk(mutex);
		for (size_t k = 0; k < numBanks; k++)
		{
			size_t i = (from + k) % numBanks;
			if (availables[i])
			{
				availables[i] = false;
				return i;
			}
		}
		return none;
	}
	void toggleAvailable(size_t i)
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
			availables[i] = true;
		}
		triggerDistSys();
	}
	Bank& bank(size_t i) { return banks.at(i); }
	void setDistPtr(RequestDistributionSystem* d) { dist = d; }
private:
	RequestDistributionSystem* dist = nullptr;
	std::deque< Bank > banks;  // deque: Bank нельзя копировать и переносить
	std::vector< bool > availables;
	std::mutex mutex;
	void triggerDistSys();
};

// ---------------------------------------------------------------- диспетчер
class RequestDistributionSystem
{
public:
	RequestDistributionSystem(RequestBuffer* b, BanksManageSystem* m) :
		buf(b),
		man(m)
	{}
	void addRequestInBuf()
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
			++numRequestInBuf;
		}
		cv.notify_one();
	}
	// Заявка вытеснена из буфера (отказ); число заявок в буфере не меняется
	void registerRejected()
	{
		std::lock_guard<std::mutex> lk(mutex);
		++rejectedRequests;
		++allRequests;
		if (allRequests % 100 == 0)
		{
			print();
		}
	}
	void addAvailableBank()
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
			++numAvailableBanks;
		}
		cv.notify_one();
	}
	void wake()
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
		}
		cv.notify_all();
	}
	void run()
	{
		std::unique_lock<std::mutex> lk(mutex);
		while (true)
		{
			cv.wait(lk, [this] { return !running || (numRequestInBuf != 0 && numAvailableBanks != 0); });
			if (!running)
			{
				return;
			}
			// Свободный прибор ищем по кругу, начиная с nextBank
			size_t bankId = man->chooseBank(nextBank);
			nextBank = (nextBank + 1) % numBanks;
			Request* r = buf->extractBest();
			Bank& bank = man->bank(bankId);  // none здесь невозможен: числа в счётчиках согласованы
			--numAvailableBanks;
			--numRequestInBuf;
			++proceededRequests;
			++allRequests;
			auto sendT = clock_type::now();
			r->setSendTime(sendT);
			{
				std::lock_guard<std::mutex> sl(statsMutex);
				stat_t& st = stats[r->getFamily()->getid()];
				st.allRequests++;
				st.proceededRequests++;
				st.waitTime.push_back(elapsedMs(r->getCreateTime(), sendT));
			}
			// После addRequest заявкой владеет прибор и может её удалить в любой момент,
			// поэтому всё нужное из неё берём заранее
			const size_t requestId = r->getid();
			bank.addRequest(r);
			r = nullptr;
			if (allRequests % 100 == 0)
			{
				print();
			}
			log("Request " + std::to_string(requestId) + " add to bank " + std::to_string(bank.getid()));
			log("Pointer on next bank " + std::to_string(nextBank));
		}
	}
private:
	RequestBuffer* buf;
	BanksManageSystem* man;
	size_t nextBank = 0;
	size_t numRequestInBuf = 0;
	size_t numAvailableBanks = 0;
	size_t proceededRequests = 0;
	size_t rejectedRequests = 0;
	size_t allRequests = 0;
	std::mutex mutex;
	std::condition_variable cv;

	void print() // вызывать под mutex
	{
		log("Statistics: all requests - " + std::to_string(allRequests) + " proceeded requests - "
			+ std::to_string(proceededRequests) + " rejected requests - " + std::to_string(rejectedRequests));
	}
};

void BanksManageSystem::triggerDistSys()
{
	dist->addAvailableBank();
}

void Bank::run()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::exponential_distribution<> dis(1.0);
	while (true)
	{
		Request* r = nullptr;
		{
			std::unique_lock<std::mutex> lk(mutex);
			cv.wait(lk, [this] { return toProceed != nullptr || !running; });
			if (toProceed == nullptr)
			{
				return;  // остановка, а работы нет
			}
			r = toProceed;
		}
		auto st = clock_type::now();
		int duration = static_cast<int>(dis(gen) * meanServiceMs);
		bool finished = sleepFor(std::chrono::milliseconds(duration));
		auto en = clock_type::now();
		if (finished)
		{
			log("Request " + std::to_string(r->getid()) + " was proceeded");
			std::lock_guard<std::mutex> sl(statsMutex);
			stats[r->getFamily()->getid()].processTime.push_back(elapsedMs(r->getSendTime(), en));
		}
		delete r;
		{
			std::lock_guard<std::mutex> lk(mutex);
			toProceed = nullptr;
		}
		// Время недообслуженной в момент остановки заявки тоже занимает прибор
		availableTime += elapsedMs(st, en);
		if (!finished)
		{
			return;
		}
		man->toggleAvailable(id);
	}
}

// ---------------------------------------------------------------- приём заявок
class RequestAcceptanceSystem
{
public:
	explicit RequestAcceptanceSystem(RequestBuffer* b) : buf(b) {}
	~RequestAcceptanceSystem()
	{
		while (!queue.empty())
		{
			delete queue.front();
			queue.pop();
		}
	}
	void acceptRequest(Request* r)
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
			queue.push(r);
		}
		cv.notify_one();
	}
	void setDistPtr(RequestDistributionSystem* d) { dist = d; }
	void wake()
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
		}
		cv.notify_all();
	}
	void run();
private:
	RequestBuffer* buf;
	RequestDistributionSystem* dist = nullptr;
	std::queue< Request* > queue;
	std::mutex mutex;
	std::condition_variable cv;
};

void RequestAcceptanceSystem::run()
{
	while (true)
	{
		Request* r = nullptr;
		{
			std::unique_lock<std::mutex> lk(mutex);
			cv.wait(lk, [this] { return !queue.empty() || !running; });
			if (!running)
			{
				return;
			}
			r = queue.front();
			queue.pop();
		}
		Request* evicted = buf->insertOrReplaceLast(r);
		if (evicted != nullptr)
		{
			log("Rejected " + std::to_string(evicted->getid()));
			{
				std::lock_guard<std::mutex> sl(statsMutex);
				stat_t& st = stats[evicted->getFamily()->getid()];
				st.allRequests++;
				st.rejectedRequests++;
			}
			delete evicted;
			dist->registerRejected();
		}
		else
		{
			dist->addRequestInBuf();
		}
	}
}

void Family::generateAndSendRequest()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(1, 2);
	while (running)
	{
		{
			std::lock_guard<std::mutex> lk(genMutex);
			log("Request " + std::to_string(nextRequestId) + " was generated");
			acc_->acceptRequest(new Request(this, nextRequestId++));
		}
		if (!sleepFor(std::chrono::milliseconds(dis(gen) * familyStepMs)))
		{
			return;
		}
	}
}

// ---------------------------------------------------------------- запуск
int main(int argc, char** argv)
{
	std::filesystem::path outDir = ".";
	double seconds = 15;
	int positional = 0;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		if (a == "--quiet" || a == "-q")
		{
			quiet = true;
		}
		else if (positional == 0)
		{
			outDir = a;
			positional++;
		}
		else if (positional == 1)
		{
			char* end = nullptr;
			seconds = std::strtod(a.c_str(), &end);
			if (end == a.c_str() || *end != '\0' || seconds <= 0)
			{
				std::cerr << "Некорректное число секунд: " << a << '\n';
				return 1;
			}
			positional++;
		}
		else
		{
			std::cerr << "Использование: " << argv[0] << " [каталог_результатов] [секунд] [--quiet]\n";
			return 1;
		}
	}
	std::error_code ec;
	std::filesystem::create_directories(outDir, ec);
	std::ofstream banksOut(outDir / "banks.txt");
	std::ofstream familysOut(outDir / "familys.txt");
	if (!banksOut || !familysOut)
	{
		std::cerr << "Не удаётся записать результаты в каталог " << outDir << '\n';
		return 1;
	}

	auto start = clock_type::now();
	RequestBuffer buf{ bufSize };
	RequestAcceptanceSystem acc{ &buf };
	BanksManageSystem man{};
	RequestDistributionSystem dist{ &buf, &man };
	acc.setDistPtr(&dist);
	man.setDistPtr(&dist);

	std::vector< std::unique_ptr< Family > > fs;
	for (size_t i = 0; i < numFamilys; i++)
	{
		fs.push_back(std::make_unique< Family >(i, &acc));
	}
	for (size_t i = 0; i < numBanks; i++)
	{
		dist.addAvailableBank();
	}

	std::vector< std::thread > threads;
	for (auto& f : fs)
	{
		threads.emplace_back(&Family::generateAndSendRequest, f.get());
	}
	threads.emplace_back(&RequestAcceptanceSystem::run, &acc);
	threads.emplace_back(&RequestDistributionSystem::run, &dist);
	for (size_t i = 0; i < numBanks; i++)
	{
		threads.emplace_back(&Bank::run, &man.bank(i));
	}

	std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
	auto end = clock_type::now();
	auto allTime = elapsedMs(start, end);

	// остановка: сбросить флаг, разбудить всех, дождаться потоков
	{
		std::lock_guard<std::mutex> lk(stopMutex);
		running = false;
	}
	stopCv.notify_all();
	acc.wake();
	dist.wake();
	for (size_t i = 0; i < numBanks; i++)
	{
		man.bank(i).wake();
	}
	for (auto& t : threads)
	{
		t.join();
	}

	for (size_t i = 0; i < numBanks; i++)
	{
		banksOut << "Bank " << i << ' ' << (double(man.bank(i).getAvailableTime()) / allTime) << '\n';
	}
	for (size_t i = 0; i < numFamilys; i++)
	{
		const stat_t& st = stats[i];
		familysOut << "USER " << i << " statistic: ALL: " << st.allRequests << " success: "
			<< st.proceededRequests << " declined: " << st.rejectedRequests << '\n';
		familysOut << "waitDurations: ";
		for (size_t j : st.waitTime)
		{
			familysOut << j << ' ';
		}
		familysOut << '\n' << "Processing durations: ";
		for (size_t j : st.processTime)
		{
			familysOut << j << ' ';
		}
		familysOut << '\n';
	}
	if (!banksOut.flush() || !familysOut.flush())
	{
		std::cerr << "Ошибка записи результатов\n";
		return 1;
	}
	return 0;
}

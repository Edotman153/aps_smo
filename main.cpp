#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <random>
#include <windows.h>
#include <memory>
#include <algorithm>
#include <map>
#undef min
constexpr size_t numFamilys = 5;
constexpr size_t bufSize = 10;
constexpr size_t numBanks = 4;
std::mutex coutMutex{};
struct stat_t
{
	size_t allRequests;
	size_t proceededRequests;
	size_t rejectedRequests;
	std::vector< size_t > waitTime;
	std::vector< size_t > processTime;
};
std::map< size_t, stat_t > stats;
std::mutex statsMutex;
using time_type = std::chrono::system_clock::time_point;

class Family;

class Request
{
public:
	Request(const Family* f, int id) :
		f_(f),
		id_(id),
		createTime(std::chrono::system_clock::now())
	{}
	const Family* getFamily() const { return f_; }
	size_t getid() const { return id_; }
	time_type getSendTime() const { return sendTime; }
	void setSendTime(time_type t) const { sendTime = t; }
	time_type getCreateTime() const { return createTime; }
private:
	const Family* f_;
	size_t id_;
	time_type createTime;
	mutable time_type sendTime;
};

class RequestAcceptanceSystem;

class Family
{
public:
	Family(size_t prio, RequestAcceptanceSystem* rp) :
		r(rp),
		priority(prio),
		id(0)
	{
		mutex.lock();
		id = familyid;
		++familyid;
		mutex.unlock();
		statsMutex.lock();
		stats[id];
		statsMutex.unlock();
	}
	void generateAndSendRequest();
	size_t getPriority() const { return priority; }
	size_t getid() const { return id; }
private:
	RequestAcceptanceSystem* r;
	size_t priority;
	size_t id;
	static size_t familyid;
	static size_t requestid;
	static std::mutex mutex;
};

size_t Family::familyid = 0;
size_t Family::requestid = 0;
std::mutex Family::mutex{};

class RequestBuffer
{
public:
	using data_t = std::vector< const Request* >;
	using range_t = std::pair< data_t::iterator, data_t::iterator >;
	using iter_t = data_t::iterator;
	iter_t insertRequest(const Request* r)
	{
		mutex.lock();
		if (currSize < bufSize)
		{
			data.push_back(r);
			++currSize;
			auto res = data.end() - 1;
			coutMutex.lock();
			std::cout << "Inserted " << r->getid() << '\n';
			coutMutex.unlock();
			mutex.unlock();
			return res;
		}
		else
		{
			auto res = data.end();
			mutex.unlock();
			return res;
		}
	}
	range_t getData()
	{
		mutex.lock();
		auto beg = data.begin();
		auto end = data.end();
		mutex.unlock();
		return { beg, end };
	}
	const Request* extractRequest(iter_t it)
	{
		mutex.lock();
		auto res = *it;
		auto r = std::remove(data.begin(), data.begin() + currSize, res);
		data.erase(r);
		--currSize;
		//std::cout << "Extracted " << res->getid() << '\n';
		mutex.unlock();
		return res;
	}
	void lock() const { extMutex.lock(); }
	void unlock() const { extMutex.unlock(); }
	void print()
	{
		coutMutex.lock();
		std::cout << "RequestBuffer: ";
		for (size_t i = 0; i < bufSize; i++)
		{
			std::cout << (i < currSize ? std::to_string(data[i]->getid()) : "_") << ' ';
		}
		std::cout << '\n';
		coutMutex.unlock();
	}
private:
	data_t data;
	size_t currSize;
	mutable std::mutex mutex;
	mutable std::mutex extMutex;
};

class RequestDistributionSystem;

class RequestAcceptanceSystem
{
public:
	RequestAcceptanceSystem(RequestBuffer* b) :
		buf(b)
	{}
	void acceptRequest(const Request* r)
	{
		mutex.lock();
		queue.push(r);
		//std::cout << "Pushed " << r->getid() << '\n';
		mutex.unlock();
	}
	void setDistPtr(RequestDistributionSystem* d) { dist = d; }
	void run();
private:
	RequestBuffer* buf;
	RequestDistributionSystem* dist;
	RequestBuffer::iter_t toSend;
	std::queue< const Request* > queue;
	std::mutex mutex;
	void rejectRequest(const Request* rNew)
	{
		auto data = buf->getData();
		auto r = buf->extractRequest(toSend);
		toSend = buf->insertRequest(rNew);
		coutMutex.lock();
		std::cout << "Rejected " << r->getid() << '\n';
		coutMutex.unlock();
		statsMutex.lock();
		stats[r->getFamily()->getid()].allRequests++;
		stats[r->getFamily()->getid()].rejectedRequests++;
		statsMutex.unlock();
		delete r;
	}
};

class BanksManageSystem;

class Bank
{
public:
	Bank(BanksManageSystem* m, size_t i) :
		man(m),
		id(i),
		toProceed(nullptr),
		availableTime(0),
		en(std::chrono::system_clock::now())
	{}
	void addRequest(const Request* r)
	{
		toProceed = r;
		//std::cout << "Request " << r->getid() << " sent to bank " << id << '\n';
	}
	void run();
	size_t getid() { return id; }
	size_t getAvailableTime() { return availableTime; }
private:
	BanksManageSystem* man;
	size_t id;
	const Request* toProceed;
	size_t availableTime;
	std::chrono::system_clock::time_point en;
};

class BanksManageSystem
{
public:
	using banks_t = std::vector< Bank >;
	using iter_t = banks_t::iterator;
	BanksManageSystem()
	{
		for (size_t i = 0; i < numBanks; i++)
		{
			banks.push_back(Bank{ this, i });
			availables.push_back(true);
		}
	}
	iter_t chooseBank(iter_t wantToSend)
	{
		mutex.lock();
		size_t I = wantToSend - banks.begin();
		for (size_t i = I; i < numBanks; i++)
		{
			if (availables[i])
			{
				//std::cout << "Bank " << i << " was chosen\n";
				availables[i] = false;
				mutex.unlock();
				return banks.begin() + i;
			}
		}
		for (size_t i = 0; i < I; i++)
		{
			if (availables[i])
			{
				//std::cout << "Bank " << i << " was chosen\n";
				availables[i] = false;
				mutex.unlock();
				return banks.begin() + i;
			}
		}
		auto res = banks.end();
		//std::cout << "Bank wasn't chosen\n";
		mutex.unlock();
		return res;
	}
	void toggleAvailable(size_t i)
	{
		mutex.lock();
		iter_t bank = banks.begin() + i;
		availables[i] = true;
		//std::cout << "Bank " << i << " toggled\n";
		mutex.unlock();
		triggerDistSys(bank);
	}
	iter_t getEndIt() { return banks.end(); }
	void setDistPtr(RequestDistributionSystem* d) { dist = d; }
private:
	RequestDistributionSystem* dist;
	banks_t banks;
	std::vector< bool > availables;
	std::mutex mutex;
	void triggerDistSys(iter_t bank);
	
};

class RequestDistributionSystem
{
public:
	RequestDistributionSystem(RequestBuffer* b, BanksManageSystem* m, BanksManageSystem::iter_t send) :
		buf(b),
		man(m),
		toSend(send)
	{}
	void addRequestInBuf()
	{
		mutex.lock();
		++numRequestInBuf;
		//std::cout << "Added request to dist sys\n";
		mutex.unlock();
	}
	void delRequestFromBuf()
	{
		mutex.lock();
		--numRequestInBuf;
		++rejectedRequests;
		++allRequests;
		if (allRequests % 100 == 0)
		{
			print();
		}
		//std::cout << "Deleted request from dist sys\n";
		mutex.unlock();
	}
	void addAvailableBank(BanksManageSystem::iter_t bank)
	{
		mutex.lock();
		++numAvailableBanks;
		toSend = bank;
		//std::cout << "Added available bank to dist sys\n";
		mutex.unlock();
	}
	void run()
	{
		while (true)
		{
			if (!(numRequestInBuf != 0 && numAvailableBanks != 0))
			{
				//Sleep(1);
				continue;
			}
			else
			{
				mutex.lock();
				auto it = man->chooseBank(toSend);
				auto r = chooseRequest();
				(*it).addRequest(r);
				--numAvailableBanks;
				--numRequestInBuf;
				++proceededRequests;
				++allRequests;
				statsMutex.lock();
				stats[r->getFamily()->getid()].allRequests++;
				stats[r->getFamily()->getid()].proceededRequests++;
				auto sendT = std::chrono::system_clock::now();
				r->setSendTime(sendT);
				auto t = (std::chrono::duration_cast<std::chrono::milliseconds>(sendT - r->getCreateTime())).count();
				stats[r->getFamily()->getid()].waitTime.push_back(t);
				statsMutex.unlock();
				if (allRequests % 100 == 0)
				{
					print();
				}
				coutMutex.lock();
				std::cout << "Request " << r->getid() << " add to bank " << it->getid() << '\n';
				coutMutex.unlock();
				mutex.unlock();
			}

		}
	}
private:
	RequestBuffer* buf;
	BanksManageSystem* man;
	BanksManageSystem::iter_t toSend;
	size_t numRequestInBuf;
	size_t numAvailableBanks;
	std::mutex mutex;
	size_t proceededRequests;
	size_t rejectedRequests;
	size_t allRequests;
	const Request* chooseRequest()
	{
		buf->lock();
		auto d = buf->getData();
		size_t maxPrio = 0;
		size_t I = 0;
		size_t minid = -1;//max size_t
		for (auto it = d.first; it < d.first + numRequestInBuf; ++it)
		{
			if (*it != nullptr)
			{
				size_t prio = (*it)->getFamily()->getPriority();
				size_t id = (*it)->getid();
				if (prio > maxPrio)
				{
					maxPrio = prio;
					I = it - d.first;
					minid = id;
				}
				if (id < minid)
				{
					minid = id;
					I = it - d.first;
				}
			}
		}
		auto res = buf->extractRequest(d.first + I);
		if (res != nullptr)
		{
			coutMutex.lock();
			std::cout << "Request " << res->getid() << " was chosen\n";
			coutMutex.unlock();
		}
		buf->unlock();
		return res;
	}
	void print()
	{
		coutMutex.lock();
		std::cout << "Statistics: all requests - " << allRequests << " proceeded requests - ";
		std::cout << proceededRequests << " rejected requests - " << rejectedRequests << '\n';
		coutMutex.unlock();
	}
};

void Family::generateAndSendRequest()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(1, 2);
	int random_number = 0;
	while (true)
	{
		random_number = dis(gen);
		Sleep(random_number * 50);
		mutex.lock();
		coutMutex.lock();
		std::cout << "Request " << requestid << " was generated\n";
		coutMutex.unlock();
		const Request* req = new Request(this, requestid++);
		r->acceptRequest(req);
		mutex.unlock();
	}
}

void Bank::run()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::exponential_distribution<> dis(1.7);
	int random_number = 0;
	while (true)
	{
		if (toProceed == nullptr)
		{
			//Sleep(1);
			continue;
		}
		auto st = std::chrono::system_clock::now();
		availableTime += (std::chrono::duration_cast<std::chrono::milliseconds>(st - en)).count();
		random_number = dis(gen) * 100;
		Sleep(random_number);
		coutMutex.lock();
		std::cout << "Request " << toProceed->getid() << " was proceeded\n";
		coutMutex.unlock();
		en = std::chrono::system_clock::now();
		statsMutex.lock();
		auto t = (std::chrono::duration_cast<std::chrono::milliseconds>(en - toProceed->getSendTime())).count();
		stats[toProceed->getFamily()->getid()].processTime.push_back(t);
		statsMutex.unlock();
		delete toProceed;
		toProceed = nullptr;
		man->toggleAvailable(id);
	}
}

void BanksManageSystem::triggerDistSys(iter_t bank)
{
	dist->addAvailableBank(bank);
}

void RequestAcceptanceSystem::run()
{
	while (true)
	{
		if (queue.empty())
		{
			//Sleep(1);
			continue;
		}
		mutex.lock();
		auto r = queue.front();
		buf->lock();
		buf->print();
		auto it = buf->insertRequest(r);
		if (it == buf->getData().second)
		{
			rejectRequest(r);
			buf->print();
			buf->unlock();
			dist->delRequestFromBuf();
		}
		else
		{
			toSend = it;
			buf->print();
			buf->unlock();
		}
		dist->addRequestInBuf();
		queue.pop();
		mutex.unlock();
	}
}

int main() {
	RequestBuffer buf{};
	RequestAcceptanceSystem acc{ &buf };
	Family fs[numFamilys]{ {1, &acc}, {2, &acc}, {3, &acc}, {4, &acc}, {5, &acc} };
	BanksManageSystem man{};
	auto beg = man.getEndIt() - numBanks;
	RequestDistributionSystem dist{ &buf, &man, beg };
	acc.setDistPtr(&dist);
	man.setDistPtr(&dist);
	auto start = std::chrono::system_clock::now();

	std::thread tf[numFamilys];
	for (size_t i = 0; i < numFamilys; i++)
	{
		tf[i] = std::thread{ &Family::generateAndSendRequest, fs + i };
	}
	std::thread t1{ &RequestAcceptanceSystem::run, &acc };
	std::thread t2{ &RequestDistributionSystem::run, &dist };
	std::thread tb[numBanks];
	for (size_t i = 0; i < numBanks; i++)
	{
		tb[i] = std::thread{ &Bank::run, &(beg[i])};
		dist.addAvailableBank(beg + i);
	}
	Sleep(10000);
	auto end = std::chrono::system_clock::now();
	auto allTime = (std::chrono::duration_cast<std::chrono::milliseconds>(end - start)).count();
	std::ofstream out;
	out.open("C:\\trash\\aps\\aps_smo\\banks.txt");
	for (size_t i = 0; i < numBanks; i++)
	{
		out << "Bank " << i << ' ' << 1 - (float(beg[i].getAvailableTime()) / allTime) << '\n';
	}
	out.close();
	out.open("C:\\trash\\aps\\aps_smo\\familys.txt");
	for (size_t i = 0; i < numFamilys; i++) {
		out << "USER " << i << " statistic: ALL: " << stats[i].allRequests << " success: "
			<< stats[i].proceededRequests<< " declined: " << stats[i].rejectedRequests << '\n';
		out << "waitDurations: ";
		for (size_t j : stats[i].processTime) {
			out << j << ' ';
		}
		out << '\n' << "Processing durations: ";
		for (size_t j : stats[i].waitTime) {
			out << j << ' ';
		}
		out << '\n';
	}
	out.close();
	statsMutex.lock();

	statsMutex.unlock();
	t1.join();
	return 0;
}

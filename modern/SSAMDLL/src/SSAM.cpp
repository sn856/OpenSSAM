#include "stdafx.h"
#include "SSAM.h"
#include<iostream>
#include<set>
#include<cfloat>
#include<cmath>
#include <iomanip>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <deque>
#include <algorithm>
#include <exception>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;
using namespace SSAMFuncs;

// Section timing for profiling. Printed only when env var SSAM_PROFILE is set.
namespace
{
	typedef std::chrono::steady_clock ProfClock;
	double g_ProfLinkSec = 0, g_ProfGridSec = 0, g_ProfEventSec = 0, g_ProfReadSec = 0;
	long long g_ProfVehicles = 0, g_ProfEventCalls = 0, g_ProfSteps = 0;
	double ProfSince(ProfClock::time_point t0)
	{
		return std::chrono::duration<double>(ProfClock::now() - t0).count();
	}

	// Chunk size for reading little-endian TRJ files. Tunable; must be larger than one record.
	const size_t TRJ_CHUNK_SIZE = 4 * 1024 * 1024;

	// Reads a TRJ file in large chunks and hands out records from memory.
	// A record that straddles a chunk boundary is completed by the next chunk.
	class TrjChunkReader
	{
	public:
		explicit TrjChunkReader(std::ifstream& in)
			: m_In(in), m_Buf(TRJ_CHUNK_SIZE), m_Pos(0), m_End(0)
		{}

		// Returns a pointer to the next n bytes and consumes them.
		// Returns NULL, consuming nothing, when the file has fewer than n bytes left.
		const char* Take(size_t n)
		{
			if (m_End - m_Pos < n && !Fill(n))
				return NULL;
			const char* p = &m_Buf[m_Pos];
			m_Pos += n;
			return p;
		}
	private:
		bool Fill(size_t n)
		{
			size_t rest = m_End - m_Pos;
			if (rest > 0 && m_Pos > 0)
				memmove(&m_Buf[0], &m_Buf[m_Pos], rest);
			m_Pos = 0;
			m_End = rest;
			while (m_End < n && m_In.good())
			{
				m_In.read(&m_Buf[m_End], std::streamsize(m_Buf.size() - m_End));
				m_End += size_t(m_In.gcount());
			}
			return m_End >= n;
		}

		std::ifstream& m_In;
		std::vector<char> m_Buf;
		size_t m_Pos, m_End;
	};

	inline int ChunkInt(const char* p)
	{
		int x = 0;
		memcpy(&x, p, sizeof(int));
		return x;
	}

	inline float ChunkFloat(const char* p)
	{
		float x = 0.0;
		memcpy(&x, p, sizeof(float));
		return x;
	}

	// Closes the TRJ file and the TRJ text file when Analyze() is left by an exception.
	struct TrjFileCloser
	{
		TrjFileCloser(std::ifstream& trj, std::ofstream& dat) : m_Trj(trj), m_Dat(dat) {}
		~TrjFileCloser()
		{
			if (m_Trj.is_open())
				m_Trj.close();
			if (m_Dat.is_open())
				m_Dat.close();
		}
		std::ifstream& m_Trj;
		std::ofstream& m_Dat;
	};

	// Time the analysis side waited for the reader thread.
	double g_ProfWaitSec = 0;
	bool g_ProfReaderOn = false;

	// Number of time steps the reader thread may read ahead of the analysis. Tunable.
	const size_t TRJ_READ_AHEAD_STEPS = 64;

	// Linking, projections and overlap checks are split across threads only when nthreads >= 2.
	// Every parallel region uses nthreads threads: changing the team size between regions is slow.
	// Smallest number of vehicles in a time step for which projections and overlap checks
	// are split across threads. Smaller steps run on the calling thread. Tunable.
	const int PARALLEL_MIN_VEHICLES = 64;
	// Smallest number of ID index entries per thread for which linking is split across threads.
	// Otherwise linking runs on the calling thread. Tunable.
	const int LINKS_PER_THREAD = 256;

	// Keeps the exception of the lowest index thrown inside a parallel region.
	// Rethrow() throws it again on the calling thread after the region.
	class ParallelError
	{
	public:
		ParallelError() : m_Index(-1) {}
		// Call inside a catch block.
		void Set(int index)
		{
#ifdef _OPENMP_LOCAL
			#pragma omp critical (PARALLELERROR)
#endif
			if (m_Index < 0 || index < m_Index)
			{
				m_Index = index;
				m_Error = std::current_exception();
			}
		}
		bool HasError() const { return m_Index >= 0; }
		void Rethrow()
		{
			if (m_Index >= 0)
				std::rethrow_exception(m_Error);
		}
	private:
		int m_Index;
		std::exception_ptr m_Error;
	};

	// False when env var SSAM_READER_THREAD is "0": read on the calling thread.
	bool UseReaderThread()
	{
		const char* v = std::getenv("SSAM_READER_THREAD");
		return !(v != NULL && std::strcmp(v, "0") == 0);
	}

	// One hand-over from the reader thread to the analysis side.
	struct TrjStepItem
	{
		enum Kind { TIMESTEP, END, FAILED };
		Kind m_Kind;
		float m_Time; // TIMESTEP: time of the time step record just read
		SP_TimeStepData m_pStep; // TIMESTEP: the previous, complete step (may be NULL); END, FAILED: the step being read
		std::exception_ptr m_Error; // FAILED: the read error
		TrjStepItem() : m_Kind(END), m_Time(0) {}
	};

	// Thrown in the reader thread when the analysis side stopped the queue.
	struct TrjReaderStopped {};

	// Bounded queue from the reader thread to the analysis side.
	class TrjStepQueue
	{
	public:
		TrjStepQueue() : m_Stopped(false), m_ReaderDone(false) {}

		// Reader side. Waits while the queue is full. Throws TrjReaderStopped after Stop().
		void Push(TrjStepItem&& item)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_NotFull.wait(lock, [this] { return m_Stopped || m_Items.size() < TRJ_READ_AHEAD_STEPS; });
			if (m_Stopped)
				throw TrjReaderStopped();
			m_Items.push_back(std::move(item));
			m_NotEmpty.notify_one();
		}

		// Analysis side. Waits while the queue is empty and adds the wait time to waitSec.
		TrjStepItem Pop(double& waitSec)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			if (m_Items.empty())
			{
				ProfClock::time_point t0 = ProfClock::now();
				m_NotEmpty.wait(lock, [this] { return !m_Items.empty() || m_ReaderDone; });
				waitSec += ProfSince(t0);
			}
			if (m_Items.empty())
			{
				// The reader ended without handing over END or FAILED (its last Push threw).
				TrjStepItem failed;
				failed.m_Kind = TrjStepItem::FAILED;
				failed.m_Error = std::make_exception_ptr(SSAMException("TRJ reader thread ended unexpectedly."));
				return failed;
			}
			TrjStepItem item = std::move(m_Items.front());
			m_Items.pop_front();
			m_NotFull.notify_one();
			return item;
		}

		void Stop()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stopped = true;
			m_NotFull.notify_all();
		}

		// Reader side, last call of the reader thread on every path.
		void MarkReaderDone()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_ReaderDone = true;
			m_NotEmpty.notify_all();
		}
	private:
		std::mutex m_Mutex;
		std::condition_variable m_NotFull, m_NotEmpty;
		std::deque<TrjStepItem> m_Items;
		bool m_Stopped;
		bool m_ReaderDone;
	};

	// Stops and joins the reader thread when the analysis side is left by an exception.
	struct TrjReaderJoiner
	{
		TrjReaderJoiner(TrjStepQueue& q, std::thread& th) : m_Queue(q), m_Thread(th) {}
		~TrjReaderJoiner()
		{
			if (m_Thread.joinable())
			{
				m_Queue.Stop();
				m_Thread.join();
			}
		}
		TrjStepQueue& m_Queue;
		std::thread& m_Thread;
	};
}

const float SSAM::DEFAULT_TTC = 1.5; 
const float SSAM::DEFAULT_PET = 5.0; 

SSAM::SSAM()
	: m_MaxTTC(DEFAULT_TTC)
	, m_MaxPET(DEFAULT_PET)
	, m_RearEndAngleThreshold(DEFAULT_REARENDANGLE)
	, m_CrossingAngleThreshold(DEFAULT_CROSSINGANGLE)
	, m_NThreads (DefaultNThreads())
	, m_IsWriteDat(false)
	, m_IsCalcPUEA(false)
	, m_Units(0)
	, m_ZoneSize(50.0)
	, m_AnalysisTime(0)
	, m_StartTime(0)
	, m_EndTime(0)
	, m_IsFirstTimeStep(true)
	, m_NSteps(10)
	, m_IsPrintProgress (false)
	, m_pDimensions (NULL)
	, m_pCurStep (NULL)
{
	m_Boundary[0] = INT_MAX;
	m_Boundary[1] = INT_MAX;
	m_Boundary[2] = INT_MIN;
	m_Boundary[3] = INT_MIN;
}

SSAM::~SSAM()
{
}

void SSAM::Initialize()
{
	if (m_MaxPET <= 0)
		throw SSAMException("Maximum PET must be greater than 0.");
	if (m_NThreads < 1)
		throw SSAMException("Number of threads must be 1 or greater.");

	m_InitEventParams.m_V1 = NULL;
	m_InitEventParams.m_V2 = NULL;
	m_InitEventParams.m_MaxTTC = m_MaxTTC;
	m_InitEventParams.m_MaxPET = m_MaxPET;
	m_InitEventParams.m_RearEndAngleThreshold = m_RearEndAngleThreshold;
	m_InitEventParams.m_CrossingAngleThreshold = m_CrossingAngleThreshold;
	m_InitEventParams.m_IsCalcPUEA = m_IsCalcPUEA;
	m_InitEventParams.m_NSteps = m_NSteps;
	m_InitEventParams.m_CollisionThreshold = 0;
	m_InitEventParams.m_pNormalAdaption = NULL;
	m_InitEventParams.m_pEvasiveAction = NULL;
	m_StartTime = std::clock();
}

void SSAM::Terminate()
{
	CalcSummaries();
	m_EndTime = std::clock();	
	m_AnalysisTime = m_EndTime - m_StartTime;

	// Verification-only dump of per-conflict P(UEA) values. Written only when SSAM_VERIFY_DUMP names a file.
	const char* verifyDumpPath = std::getenv("SSAM_VERIFY_DUMP");
	if (verifyDumpPath != NULL && verifyDumpPath[0] != '\0')
	{
		std::ofstream dump(verifyDumpPath);
		dump << std::setprecision(9);
		for (std::list<SP_Conflict>::iterator it = m_ConflictList.begin();
			it != m_ConflictList.end(); ++it)
		{
			SP_Conflict c = *it;
			dump << c->trjFile << "," << c->FirstVID << "," << c->SecondVID << ","
				<< c->tMinTTC << "," << c->PUEA << "," << c->mTTC << "," << c->mPET << "\n";
		}
	}

	if (std::getenv("SSAM_PROFILE") != NULL)
	{
		double total = double(m_AnalysisTime) / CLOCKS_PER_SEC;
		std::cout << "[profile] total=" << total << "s"
			<< " link=" << g_ProfLinkSec << "s"
			<< " grid=" << g_ProfGridSec << "s"
			<< " events=" << g_ProfEventSec << "s"
			<< " read=" << g_ProfReadSec << "s"
			<< " other=" << (total - g_ProfLinkSec - g_ProfGridSec - g_ProfEventSec - g_ProfReadSec) << "s\n"
			<< "[profile] steps=" << g_ProfSteps
			<< " vehicleRecords=" << g_ProfVehicles
			<< " eventAnalyzeCalls=" << g_ProfEventCalls
			<< " conflicts=" << m_ConflictList.size() << std::endl;
		std::cout << "[profile] reader=" << (g_ProfReaderOn ? "on" : "off")
			<< " wait=" << g_ProfWaitSec << "s" << std::endl;
	}
}

void SSAM::Analyze()
{
	if (!m_TrjFileNames.empty())
		Analyze(m_TrjFileNames);

	if (!m_TrjDataLists.empty())
		Analyze(m_TrjDataLists);
}

void SSAM::Analyze(const std::list<std::string>& trjFileNames)
{
	Initialize();
	// Profile totals are per analysis run (the GUI may analyze several times in one process).
	g_ProfLinkSec = g_ProfGridSec = g_ProfEventSec = g_ProfReadSec = g_ProfWaitSec = 0;
	g_ProfVehicles = g_ProfEventCalls = g_ProfSteps = 0;
	g_ProfReaderOn = false;
	for (std::list<std::string>::const_iterator it = trjFileNames.begin();
		it != trjFileNames.end(); ++it)
	{
		if (it->empty())
			continue;

		m_TrjSrcName = *it;
		if (m_TrjSrcName.substr(m_TrjSrcName.length() - 4, 4) != ".trj")
			throw SSAMException("File \"" + m_TrjSrcName + "\" is not a .trj file."); 

		TrjFileCloser fileCloser(m_TrjFile, m_DatFile);
		m_TrjFile.open(m_TrjSrcName, std::ifstream::binary);
		if(!m_TrjFile.good()) 
			throw SSAMException("File \"" + m_TrjSrcName + "\" does not exist.");

		if (m_IsWriteDat)
			m_DatFile.open(m_TrjSrcName.substr(0, m_TrjSrcName.length() - 4)+"_dat.csv");

		ReadTrjInputSize();

		int recordType = ReadByte(m_TrjFile);
		if(recordType != TrjRecord::FORMAT)
			throw SSAMException("Read error, expected FORMAT data not found.");
		ReadInputFormat();
		ValidateInputFormat();

		recordType = ReadByte(m_TrjFile);
		if(recordType != TrjRecord::DIMENSIONS)
			throw SSAMException("Read error, expected DIMENSIONS data not found.");
		ReadDimensions(); 
		m_pDimensions->Validate();
		if (m_IsWriteDat)
			m_pDimensions->Print(m_DatFile);
		ApplyDimensions();
		
		m_ReadTimeStep = -1;
		m_AnalysisTimeStep = -1;
		m_IsFirstTimeStep = true;
		m_StepDataList.clear();
		m_EventList.clear();
		m_pCurStep = NULL;
		
		ProfClock::time_point profRead0 = ProfClock::now();
		double profStepSec = 0;
		// Reads the body records. onTimeStep(t) handles a time step record; curStep receives the vehicles.
		auto readBody = [&](SP_TimeStepData& curStep, auto onTimeStep)
		{
			if (m_Format.m_Endian == 'L')
			{
				// Little-endian file: read in chunks and take the values from memory.
				const bool hasZ = m_Format.m_Version > ORIG_FORMAT_VERSION;
				const size_t vehicleSize = 2 * INT_SIZE + 1 + (hasZ ? 10 : 8) * FLOAT_SIZE;
				TrjChunkReader reader(m_TrjFile);
				const char* p;
				while ((p = reader.Take(1)) != NULL)
				{
					int recordType = *p;
					if (recordType == TrjRecord::TIMESTEP)
					{
						p = reader.Take(FLOAT_SIZE);
						if (p == NULL)
							throw SSAMException("file reading failed");
						onTimeStep(ChunkFloat(p));
					} else if (recordType == TrjRecord::VEHICLE)
					{
						SP_Vehicle v = std::make_shared<Vehicle>();
						v->SetTimeStep(curStep->GetTimestep());
						p = reader.Take(vehicleSize);
						if (p == NULL)
						{
							SSAMException e("file reading failed");
							std::string errMsg("Error in reading vehicle record: ");
							ThrowFileReadingException(e, errMsg);
						}
						// Same order of calls as ReadVehicle().
						v->SetVehicleID(ChunkInt(p));
						v->SetLinkID(ChunkInt(p + INT_SIZE));
						v->SetLaneID(p[2 * INT_SIZE]);
						const char* pf = p + 2 * INT_SIZE + 1;
						float vFrontX = ChunkFloat(pf);
						float vFrontY = ChunkFloat(pf + FLOAT_SIZE);
						float vRearX = ChunkFloat(pf + 2 * FLOAT_SIZE);
						float vRearY = ChunkFloat(pf + 3 * FLOAT_SIZE);
						v->setScale(m_pDimensions->GetScale());
						v->SetLength(ChunkFloat(pf + 4 * FLOAT_SIZE));
						v->SetWidth(ChunkFloat(pf + 5 * FLOAT_SIZE));
						v->SetSpeed(ChunkFloat(pf + 6 * FLOAT_SIZE));
						v->SetAcceleration(ChunkFloat(pf + 7 * FLOAT_SIZE));
						v->SetPosition(vFrontX, vFrontY, vRearX, vRearY);
						if (hasZ)
						{
							v->SetFrontZ(ChunkFloat(pf + 8 * FLOAT_SIZE));
							v->SetRearZ(ChunkFloat(pf + 9 * FLOAT_SIZE));
						}
						if (m_IsWriteDat)
							v->Print(m_DatFile, m_Format.m_Version);
						if ( ValidateVehicle(v))
						{
							int vID = v->GetVehicleID();
							curStep->AddVehicle(vID, std::move(v));
						}
					} else
					{
						throw SSAMException("Invalid trajectory record type (outside of header): " + std::to_string(recordType));
					}
				}
			}

			// Big-endian file: the existing per-field reading.
			char typeChar;
			while (m_Format.m_Endian != 'L' && m_TrjFile.get(typeChar))
			{
				int recordType = typeChar;
				if (recordType == TrjRecord::TIMESTEP)
				{
					float t = ReadFloat(m_TrjFile);
					onTimeStep(t);
				} else if (recordType == TrjRecord::VEHICLE)
				{
					SP_Vehicle v = std::make_shared<Vehicle>();
					v->SetTimeStep(curStep->GetTimestep());
					ReadVehicle(v);
					if (m_IsWriteDat)
						v->Print(m_DatFile, m_Format.m_Version);
					if ( ValidateVehicle(v))
					{
						int vID = v->GetVehicleID();
						curStep->AddVehicle(vID, std::move(v));
					}
				} else
				{
					throw SSAMException("Invalid trajectory record type (outside of header): " + std::to_string(recordType));
				}
			}
		};

		if (!UseReaderThread())
		{
			// Read and analyze on the calling thread.
			readBody(m_pCurStep, [&](float t)
			{
				ProfClock::time_point profStep0 = ProfClock::now();
				SetTimeStep(t);
				profStepSec += ProfSince(profStep0);
			});
		} else
		{
			// The reader thread reads, builds the steps and writes the TRJ text file.
			// The calling thread analyzes the steps in file order, as SetTimeStep() does.
			g_ProfReaderOn = true;
			TrjStepQueue queue;
			std::thread readerThread([&]
			{
				SP_TimeStepData readStep;
				try
				{
					readBody(readStep, [&](float t)
					{
						// SetTimeStep() without the analysis.
						size_t nExpected = readStep ? readStep->GetVehicleVec()->size() : 0;
						TrjStepItem item;
						item.m_Kind = TrjStepItem::TIMESTEP;
						item.m_Time = t;
						item.m_pStep = std::move(readStep);
						readStep = std::make_shared<TimeStepData>();
						readStep->Reserve(nExpected);
						readStep->SetTimestep(t);
						if (m_IsWriteDat)
							PrintTimeStep(t);
						m_IsFirstTimeStep = false;
						queue.Push(std::move(item));
					});
					TrjStepItem item;
					item.m_Kind = TrjStepItem::END;
					item.m_pStep = readStep;
					queue.Push(std::move(item));
				} catch (const TrjReaderStopped&)
				{
				} catch (...)
				{
					try
					{
						TrjStepItem item;
						item.m_Kind = TrjStepItem::FAILED;
						item.m_pStep = readStep;
						item.m_Error = std::current_exception();
						queue.Push(std::move(item));
					} catch (...)
					{
					}
				}
				queue.MarkReaderDone();
			});
			TrjReaderJoiner readerJoiner(queue, readerThread);
			double waitSec = 0;
			for (;;)
			{
				TrjStepItem item = queue.Pop(waitSec);
				if (item.m_Kind != TrjStepItem::TIMESTEP)
				{
					readerThread.join();
					g_ProfWaitSec += waitSec;
					m_pCurStep = item.m_pStep;
					if (item.m_Kind == TrjStepItem::FAILED)
						std::rethrow_exception(item.m_Error);
					break;
				}
				// SetTimeStep() on the analysis side.
				ProfClock::time_point profStep0 = ProfClock::now();
				m_pCurStep = std::move(item.m_pStep);
				if (m_pCurStep)
				{
					AnalyzeOneStep();
				}
				if (m_IsPrintProgress && fmod(item.m_Time, 100) == 0)
				{
					std::cout << "Time: " << item.m_Time <<std::endl;
				}
				profStepSec += ProfSince(profStep0);
			}
		}
		g_ProfReadSec += ProfSince(profRead0) - profStepSec;
		CloseRun();
		m_TrjFile.close();
	}
	Terminate();
}

void SSAM::Analyze(const std::list<TrjDataList>& trjDataLists)
{
	Initialize();
	for (std::list<TrjDataList>::const_iterator it = trjDataLists.begin();
		it != trjDataLists.end(); ++it)
	{
		if (it->second->empty())
			continue;

		SetTrjSrcName(it->first);
		m_TrjDataList = *(it->second);
		ReadTrjInputSize();

		std::list<TrjRecord>::iterator itRec = m_TrjDataList.begin();
		if(itRec->GetRecordType() != TrjRecord::FORMAT)
			throw SSAMException("Expected FORMAT data not found.");
		SetFormat(itRec->GetFormat());
		
		itRec++;
		if(itRec == m_TrjDataList.end() 
			|| itRec->GetRecordType() != TrjRecord::DIMENSIONS)
			throw SSAMException("Expected DIMENSIONS data not found.");
		SetDimensions(itRec->GetDimensions());
		
		itRec++;
		while (itRec != m_TrjDataList.end()) 
		{
			int recordType = itRec->GetRecordType();
			if (recordType == TrjRecord::TIMESTEP)
			{
				SetTimeStep(itRec->GetTimestep());
			} else if (recordType == TrjRecord::VEHICLE)
			{
				SetVehicle(itRec->GetVehicle());
			} else
			{
				throw SSAMException("Invalid trajectory record type (outside of header): " + std::to_string(recordType));
			}
			itRec++;
		}
		CloseRun();
	}
	Terminate();
}

void SSAM::SetTrjSrcName(const std::string& s) 
{ 
	m_TrjSrcName = s;
	
	// if convert to csv file, open the csv file
	if (m_IsWriteDat)
		m_DatFile.open(m_TrjSrcName+"_dat.csv");
}

void SSAM::SetFormat(const InputFormat& f)
{
	m_Format = f;
	ValidateInputFormat();
}

void SSAM::SetDimensions(SP_Dimensions pDims)
{
	m_pDimensions = std::make_shared<Dimensions>(*pDims);
	m_pDimensions->Validate();
	if (m_IsWriteDat)
		m_pDimensions->Print(m_DatFile);
	ApplyDimensions();
}

void SSAM::SetTimeStep(float t)
{
	if (m_IsFirstTimeStep)
	{
		m_ReadTimeStep = -1;
		m_AnalysisTimeStep = -1;
		m_StepDataList.clear();
		m_EventList.clear();
		m_pCurStep = NULL;
	}

	if (m_pCurStep)
	{
		AnalyzeOneStep();
	}
				
	// The previous time step is the best guess for the number of vehicles of the new one.
	size_t nExpected = m_pCurStep ? m_pCurStep->GetVehicleVec()->size() : 0;
	m_pCurStep = std::make_shared<TimeStepData>();
	m_pCurStep->Reserve(nExpected);
	m_pCurStep->SetTimestep(t); // 
	if (m_IsPrintProgress && fmod(t, 100) == 0)
	{
		std::cout << "Time: " << t <<std::endl;
	}

	if (m_IsWriteDat)
		PrintTimeStep(t);

	if (m_IsFirstTimeStep)
	{
		m_IsFirstTimeStep = false;
	}
}

void SSAM::SetVehicle(SP_Vehicle pVeh)
{
	SP_Vehicle v = std::make_shared<Vehicle>(*pVeh);
	if (m_IsWriteDat)
		v->Print(m_DatFile, m_Format.m_Version);
	if ( ValidateVehicle(v)) 
	{
		int vID = v->GetVehicleID();
		m_pCurStep->AddVehicle(vID, std::move(v));
	}
}

void SSAM::CloseRun()
{
	// check whether to process the last set of steps and proceed if enough data
	if (m_pCurStep)
	{
		AnalyzeOneStep();
	}
		 
	if (m_IsWriteDat)
		m_DatFile.close();

	m_IsFirstTimeStep = true;
}

void SSAM::ApplyDimensions()
{
	m_Units = m_pDimensions->GetUnits();
	if(m_Units == Dimensions::ENGLISH_UNITS)
		m_ZoneSize = (int)(50.0/m_pDimensions->GetScale());
	else if(m_Units == Dimensions::METRIC_UNITS)
		m_ZoneSize = (int)(15.0/m_pDimensions->GetScale());
		
	if (m_pZoneGrid == NULL)
	{
		m_pZoneGrid = std::make_shared<ZoneGrid>(m_pDimensions->GetMinX(), 
			m_pDimensions->GetMinY(), 
			m_pDimensions->GetMaxX(),
			m_pDimensions->GetMaxY(),
			m_ZoneSize);
	} else
	{
		m_pZoneGrid->ResetGrid(m_pDimensions->GetMinX(), 
			m_pDimensions->GetMinY(), 
			m_pDimensions->GetMaxX(),
			m_pDimensions->GetMaxY(),
			m_ZoneSize);
	}

	if (m_pDimensions->GetMinX() < m_Boundary[0])
		m_Boundary[0] = m_pDimensions->GetMinX();

	if (m_pDimensions->GetMinY() < m_Boundary[1])
		m_Boundary[1] = m_pDimensions->GetMinY();

	if (m_pDimensions->GetMaxX() > m_Boundary[2])
		m_Boundary[2] = m_pDimensions->GetMaxX();

	if (m_pDimensions->GetMaxY() > m_Boundary[3])
		m_Boundary[3] = m_pDimensions->GetMaxY();

	if (m_IsCalcPUEA) 
	{
		int nTrajs = 100;
		int nRate = m_NSteps * m_NSteps;
		float ttcSteerMax = 0.2 / float(m_NSteps);
		float pueaSteerMax = 0.5 / float(m_NSteps);
		float maxSpeed = 75.0 * 5280.0 / 3600.0 / (float(m_NSteps));
		float collisionThreshold = 6.0;
		float ttcAccelMax = 6.56 / float(nRate);
		float pueaAccelMin = -29.86 / float(nRate);
		float pueaAccelMax = 14.11 / float(nRate);
		if (m_pDimensions->GetUnits()  == Dimensions::METRIC_UNITS)
		{
			maxSpeed = 90.0 / 3.6/ (float(m_NSteps));
			collisionThreshold = 1.8;
			ttcAccelMax = 2.0 / float(nRate);
			pueaAccelMin = -9.1 / float(nRate);
			pueaAccelMax = 4.3 / float(nRate);
		}
		using namespace MotPredNameSpace;
			
		SP_NormalAdaption normalAdaption = std::make_shared<NormalAdaption>(maxSpeed, 
			nTrajs, 
			ttcAccelMax, 
			ttcSteerMax);
			
		SP_EvasiveAction evasiveAction =  std::make_shared<EvasiveAction>(maxSpeed, 
			nTrajs, 
			pueaAccelMin,
			pueaAccelMax, 
			pueaSteerMax);

		m_InitEventParams.m_CollisionThreshold = collisionThreshold;
		m_InitEventParams.m_pNormalAdaption = normalAdaption;
		m_InitEventParams.m_pEvasiveAction = evasiveAction;
	}	
}

void SSAM::AnalyzeOneStep()
{
	m_ReadTimeStep = m_pCurStep->GetTimestep();
	ProfClock::time_point profLink0 = ProfClock::now();

	//	Link vehicles from this step to previous step
	m_pCurStep->SortIndex();
	if(!m_StepDataList.empty())
	{
		// Both ID indexes are sorted by vehicle ID: walk them together.
		const std::vector<TimeStepData::IDIndexEntry>& prevIndex = m_StepDataList.back()->GetIDIndex();
		const std::vector<TimeStepData::IDIndexEntry>& curIndex = m_pCurStep->GetIDIndex();
		std::vector<SP_Vehicle>* prevVec = m_StepDataList.back()->GetVehicleVec();
		std::vector<SP_Vehicle>* curVec = m_pCurStep->GetVehicleVec();
		// Links the previous-step entries [iBegin, iEnd) of the ID index.
		auto linkRange = [&](size_t iBegin, size_t iEnd)
		{
			if (iBegin >= iEnd)
				return;
			size_t iCur = std::lower_bound(curIndex.begin(), curIndex.end(), prevIndex[iBegin].first,
				[](const TimeStepData::IDIndexEntry& e, int id) { return e.first < id; }) - curIndex.begin();
			for(size_t iPrev = iBegin; iPrev < iEnd; ++iPrev)
			{
				const SP_Vehicle& vPrev = (*prevVec)[prevIndex[iPrev].second];
				if(vPrev == NULL)
					throw SSAMException("Step analysis failed, linking invalid vehicle from previous timestep.");
					
				int preID = prevIndex[iPrev].first;
				while (iCur < curIndex.size() && curIndex[iCur].first < preID)
					++iCur;
					
				if (iCur < curIndex.size() && curIndex[iCur].first == preID)
					vPrev->SetNext((*curVec)[curIndex[iCur].second]);
			}
		};
		int nPrev = (int)prevIndex.size();
#ifdef _OPENMP_LOCAL
		if (m_NThreads > 1 && nPrev >= m_NThreads * LINKS_PER_THREAD)
		{
			// Each thread links one block of the ID index. Each vehicle is linked once.
			int nBlocks = m_NThreads;
			ParallelError linkError;
			#pragma omp parallel for num_threads(m_NThreads) schedule(static)
			for(int iBlock = 0; iBlock < nBlocks; ++iBlock)
			{
				try
				{
					linkRange((size_t)((long long)nPrev * iBlock / nBlocks),
						(size_t)((long long)nPrev * (iBlock + 1) / nBlocks));
				}
				catch (...)
				{
					linkError.Set(iBlock);
				}
			}
			linkError.Rethrow();
		}
		else
#endif
		linkRange(0, (size_t)nPrev);
	}
	else
	{
		m_AnalysisTimeStep = m_ReadTimeStep - 1;
	}
	m_StepDataList.push_back(m_pCurStep);
	g_ProfLinkSec += ProfSince(profLink0);

	// check whether to process the set of steps and proceed if enough data
	// and then remove the first step in the current set
	while (m_ReadTimeStep - m_AnalysisTimeStep >= m_MaxPET) 
	{
		m_AnalysisTimeStep = m_StepDataList.front()->GetTimestep();
		DetectConflicts(m_pZoneGrid, m_EventList);
		//	Discard the first timestep (if any)
		if(!m_StepDataList.empty())
			m_StepDataList.pop_front();
	}
}

void SSAM::DetectConflicts(SP_ZoneGrid pZoneGrid, std::map<VehiclePair, SP_Event>& eventList)
{
	pZoneGrid->ClearGrid();
		
	SP_TimeStepData step = m_StepDataList.front();
	std::vector<SP_Vehicle>* stepVehVec = step->GetVehicleVec();
	ProfClock::time_point profGrid0 = ProfClock::now();
	g_ProfSteps += 1;
	g_ProfVehicles += (long long)stepVehVec->size();

	// Vehicles are numbered in file order. The projection of a vehicle is checked only
	// against the projections of earlier-numbered vehicles in the zones they share.
	int nVeh = (int)stepVehVec->size();
	std::vector<SP_Vehicle> projs(nVeh);
	std::vector<int> ranges(4 * (size_t)nVeh);

	// Creates or updates the events of vehicle v with the vehicles of the crash IDs, in ID order.
	auto addEvents = [&](const SP_Vehicle& v, const std::vector<int>& crashIDs)
	{
		for (size_t k = 0; k < crashIDs.size(); ++k)
		{
			SP_Vehicle vActual = NULL;
			const SP_Vehicle* pActual = step->FindVehicle(crashIDs[k]);
			if (pActual != NULL)
				vActual = *pActual;
			else
				continue;
				
			int idLo = min(vActual->GetVehicleID(), v->GetVehicleID());
			int idHi = max(vActual->GetVehicleID(), v->GetVehicleID());
			VehiclePair  vehPair(idLo, idHi);
			SP_Event pEvent = NULL;
			if (eventList.find(vehPair)  != eventList.end())
			{
				pEvent = eventList[vehPair]; 
				pEvent->AddVehicleData(vActual, v);
			} else
			{
				m_InitEventParams.m_V1 = vActual;
				m_InitEventParams.m_V2 = v;
				pEvent = std::make_shared<Event>(m_InitEventParams);
				eventList[vehPair] = pEvent;
			}
		}
	};

#ifdef _OPENMP_LOCAL
	if (m_NThreads > 1 && nVeh >= PARALLEL_MIN_VEHICLES)
	{
		std::vector<char> inGrid(nVeh, 0);
		std::vector<std::vector<int> > crashIDs(nVeh);
		ParallelError projError, zoneError, crashError;
		#pragma omp parallel num_threads(m_NThreads)
		{
			// 1. Projections, split across threads.
			#pragma omp for schedule(dynamic, 32)
			for(int iv = 0; iv < nVeh; ++iv)
			{
				try
				{
					projs[iv] = (*stepVehVec)[iv]->CalcProjection(m_MaxTTC,m_MaxPET);
					inGrid[iv] = pZoneGrid->GetZoneRange(projs[iv], &ranges[4 * iv]) ? 1 : 0;
				}
				catch (...)
				{
					projError.Set(iv);
				}
			}

			// 2. Zone occupancy, by the calling thread in vehicle number order.
			#pragma omp master
			{
				try
				{
					if (!projError.HasError())
					{
						for(int iv = 0; iv < nVeh; ++iv)
						{
							if (inGrid[iv])
								pZoneGrid->AddIndex(&ranges[4 * iv], iv);
						}
					}
				}
				catch (...)
				{
					zoneError.Set(0);
				}
			}
			#pragma omp barrier

			// 3. Overlap checks, split across threads. Each vehicle writes only its own list.
			#pragma omp for schedule(dynamic, 32)
			for(int iv = 0; iv < nVeh; ++iv)
			{
				try
				{
					if (inGrid[iv] && !projError.HasError() && !zoneError.HasError())
						pZoneGrid->FindCrashes(projs, iv, &ranges[4 * iv], crashIDs[iv]);
				}
				catch (...)
				{
					crashError.Set(iv);
				}
			}
		}
		projError.Rethrow();
		zoneError.Rethrow();
		crashError.Rethrow();

		// 4. Events, on the calling thread in vehicle number order.
		for(int iv = 0; iv < nVeh; ++iv)
		{
			if (!crashIDs[iv].empty())
				addEvents((*stepVehVec)[iv], crashIDs[iv]);
		}
	}
	else
#endif
	{
		// One vehicle at a time, on the calling thread.
		std::vector<int> crashIDs;
		for(int iv = 0; iv < nVeh; ++iv)
		{
			const SP_Vehicle& v = (*stepVehVec)[iv];
			projs[iv] = v->CalcProjection(m_MaxTTC,m_MaxPET);
			if (!pZoneGrid->GetZoneRange(projs[iv], &ranges[4 * iv]))
				continue;
			pZoneGrid->FindCrashes(projs, iv, &ranges[4 * iv], crashIDs);
			pZoneGrid->AddIndex(&ranges[4 * iv], iv);
			if (!crashIDs.empty())
				addEvents(v, crashIDs);
		}
	}

	g_ProfGridSec += ProfSince(profGrid0);

	if (!eventList.empty())
	{
		ProfClock::time_point profEvent0 = ProfClock::now();
		g_ProfEventCalls += (long long)eventList.size();
		AnalyzEvents(m_TrjSrcName, eventList);
		g_ProfEventSec += ProfSince(profEvent0);
	}
}

void SSAM::AnalyzEvents(const std::string& trjSrcName, std::map<VehiclePair, SP_Event>& eventList)
{
	std::map<VehiclePair, SP_Event>::iterator it = eventList.begin();
	while (it != eventList.end())
	{
		SP_Event e = it->second;
		if(e->AnalyzeData(m_AnalysisTimeStep) == false)
		{
			if(e->IsConflict())
			{
				CreateConflict(e,trjSrcName);
			}
			eventList.erase(it++);	
		}
		else
		{
			++it;
		}
	}
}

void SSAM::CalcSummaries()
{
	m_Summaries.clear();
	if (m_ConflictList.empty())
		return;

	m_pSummary = std::make_shared<Summary>("Unfiltered-All Files", m_ConflictList);
	m_Summaries.push_back(m_pSummary);
	for (std::map<std::string, std::list<SP_Conflict> >::iterator it = m_FileToConflictsMap.begin();
		it != m_FileToConflictsMap.end(); ++it)
	{
		SP_Summary pFileSummary = std::make_shared<Summary>("Unfiltered-"+it->first, it->second);
		m_Summaries.push_back(pFileSummary);
	}
}

void SSAM::ExportResults()
{
	try
	{
		if(m_ConflictList.empty())
			throw SSAMException("No conflicts to export to a .csv file.");
		
		std::ofstream csvFile(m_CsvFileName);
		csvFile << std::fixed;			
		
		csvFile << "Summary Statistics\n";
		int m = 0;
		csvFile << "Stats,";
		for(m = 0; m < Conflict::NUM_MEASURES; m++)
			csvFile << Conflict::MEASURE_LABEL[m] << ",";
		csvFile << std::endl;
		
		csvFile << "Min,";
		for(m = 0; m < Conflict::NUM_MEASURES; m++)
			csvFile << m_pSummary->GetMinVals()[m]<< ",";
		csvFile<< std::endl;
		
		csvFile << "Max,";
		for(m = 0; m < Conflict::NUM_MEASURES; m++)
			csvFile << m_pSummary->GetMaxVals()[m] << ",";
		csvFile<< std::endl;
		
		csvFile << "Mean,";
		for(m = 0; m < Conflict::NUM_MEASURES; m++)
			csvFile << m_pSummary->GetMeanVals()[m] << ",";
		csvFile<< std::endl;
				
		csvFile << "Var,";
		for(m = 0; m < Conflict::NUM_MEASURES; m++)
			csvFile << m_pSummary->GetVarVals()[m] << ",";
		csvFile<< std::endl;
		csvFile<< std::endl;

		csvFile << "Conflict Listing," <<m_ConflictList.size() << "\n";
		csvFile << "Time (min TTC),X (min PET), Y (min PET),Z (min PET), ConflictType, FristVID,SecondVID,TTC,PET,MaxS,DeltaS,DR,MaxD,xFirstCSP,yFirstCSP,xFirstCEP,yFirstCEP,xSecondCSP,ySecondCSP,xSecondCEP,ySecondCEP\n";
		
		std::list<SP_Conflict>::iterator it = m_ConflictList.begin();
		for (; it != m_ConflictList.end(); ++it)
		{
			SP_Conflict c  = *it;
			
			csvFile<<c->tMinTTC;
			csvFile<<",";
			csvFile<<c->xMinPET;
			csvFile<<",";
			csvFile<<c->yMinPET;
			csvFile<<",";
			csvFile<<c->zMinPET;
			csvFile<<",";
			csvFile<<Conflict::CONFLICT_TYPE_LABEL[c->ConflictType];
			csvFile<<",";
			csvFile<<c->FirstVID;
			csvFile<<",";
			csvFile<<c->SecondVID;
			csvFile<<",";
			csvFile<<c->TTC;
			csvFile<<",";
			csvFile<<c->PET;
			csvFile<<",";
			csvFile<<c->MaxS;
			csvFile<<",";
			csvFile<<c->DeltaS;
			csvFile<<",";
			csvFile<<c->DR;
			csvFile<<",";
			csvFile<<c->MaxD;
			csvFile<<",";
			csvFile<<c->xFirstCSP;
			csvFile<<",";
			csvFile<<c->yFirstCSP;
			csvFile<<",";
			csvFile<<c->xFirstCEP;
			csvFile<<",";
			csvFile<<c->yFirstCEP;
			csvFile<<",";
			csvFile<<c->xSecondCSP;
			csvFile<<",";
			csvFile<<c->ySecondCSP;
			csvFile<<",";
			csvFile<<c->xSecondCEP;
			csvFile<<",";
			csvFile<<c->ySecondCEP;
			csvFile<<std::endl;
		}
		csvFile.close();
			
		std::cout << "Completed exporting to CSV file: " << m_CsvFileName <<std::endl;
	}
	catch(std::runtime_error& e)
	{
		std::cerr << e.what() << std::endl;
	}
	return;
}

void SSAM::ReadTrjInputSize()
{
	int fileSize = 0;
	std::string unitStr (" bytes");
	if (m_TrjFile.is_open())
	{
		m_TrjFile.seekg(0, m_TrjFile.end);
		fileSize = m_TrjFile.tellg();
		m_TrjFile.seekg(0, m_TrjFile.beg);
	} else if (!(m_TrjDataList.empty()))
	{
		fileSize = m_TrjDataList.size();
		unitStr = " records";
	}

	if(m_IsWriteDat)
	{
		m_DatFile << "TRJ Source," << m_TrjSrcName <<std::endl;
		m_DatFile << "Input Size," << fileSize << unitStr << std::endl;
	}
}

void SSAM::ReadInputFormat()
{
	try
	{
		m_Format.m_Endian = ReadByte(m_TrjFile);
		m_Format.m_Version = ReadFloat(m_TrjFile);
		if (m_Format.m_Version > ORIG_FORMAT_VERSION)
		{
			m_Format.m_ZOption = ReadByte(m_TrjFile);
		}
	} catch (SSAMException &e)
	{
		std::string errMsg("Error in reading format record: ");
		ThrowFileReadingException(e, errMsg);
	}
}

void SSAM::ReadDimensions()
{
	try
	{
		m_pDimensions = std::make_shared<Dimensions>();
		m_pDimensions->SetUnits(ReadByte(m_TrjFile));
		m_pDimensions->SetScale(ReadFloat(m_TrjFile));
		m_pDimensions->SetMinX(ReadInt(m_TrjFile));
		m_pDimensions->SetMinY(ReadInt(m_TrjFile));
		m_pDimensions->SetMaxX(ReadInt(m_TrjFile));
		m_pDimensions->SetMaxY(ReadInt(m_TrjFile));
	} catch (SSAMException &e)
	{
		std::string errMsg("Error in reading dimension record: ");
		ThrowFileReadingException(e, errMsg);
	}
}

void SSAM::ReadVehicle(SP_Vehicle v)
{
	try
	{
		v->SetVehicleID(ReadInt(m_TrjFile));
		v->SetLinkID(ReadInt(m_TrjFile));
		v->SetLaneID(ReadByte(m_TrjFile));
		float vFrontX = ReadFloat(m_TrjFile);
		float vFrontY = ReadFloat(m_TrjFile);
		float vRearX = ReadFloat(m_TrjFile);
		float vRearY = ReadFloat(m_TrjFile);
		v->setScale(m_pDimensions->GetScale());					
		v->SetLength(ReadFloat(m_TrjFile));
		v->SetWidth(ReadFloat(m_TrjFile));
		v->SetSpeed(ReadFloat(m_TrjFile));
		v->SetAcceleration(ReadFloat(m_TrjFile));
		v->SetPosition(vFrontX, vFrontY, vRearX, vRearY);
		if (m_Format.m_Version > ORIG_FORMAT_VERSION)
		{
			v->SetFrontZ( ReadFloat(m_TrjFile));
			v->SetRearZ( ReadFloat(m_TrjFile));
		}
	} catch (SSAMException &e)
	{
		std::string errMsg("Error in reading vehicle record: ");
		ThrowFileReadingException(e, errMsg);
	}
}

void SSAM::ValidateInputFormat()
{
	if(m_Format.m_Endian < 0)
		throw SSAMException("Invalid endian encoding: " + std::to_string(m_Format.m_Endian) + ".\n");
	if(m_Format.m_Endian != 'L' && m_Format.m_Endian != 'B')
	{
		throw SSAMException("Unrecognized encoding (neither big/little endian).\n");
	} 

	if (m_IsWriteDat)
		m_Format.Print(m_DatFile);
}

bool SSAM::ValidateVehicle(const SP_Vehicle& v)
{
	return ((v->GetCenterX() >= m_pDimensions->GetMinX() 
			  && v->GetCenterX() <= m_pDimensions->GetMaxX())
			&& (v->GetCenterY() >= m_pDimensions->GetMinY() 
			  && v->GetCenterY() <= m_pDimensions->GetMaxY())) ;
}

void SSAM::ThrowFileReadingException(SSAMException &e, std::string& errMsg)
{
	if (m_TrjFile.eof())
	{
		errMsg += "End of trj file is reached. \n";
	} else 
	{
		errMsg += e.what();	
	}
	throw SSAMException(errMsg);
}

void SSAM::PrintTimeStep(float t)
{
	if (m_IsFirstTimeStep)
	{
		m_DatFile<<"Timestep,"
			<<"Vehicle ID,"
			<<"Link ID,"
			<<"Lane ID,"
			<<"Front X,"
			<<"Front Y,"
			<<"Rear X,"
			<<"Rear Y,"
			<<"Length,"
			<<"Width,"
			<<"Speed,"
			<<"Acceleration,";
		if (m_Format.m_Version > ORIG_FORMAT_VERSION)
		{
			m_DatFile << "Front Z,"
				<< "Rear Z,";
		}
		m_DatFile << std::endl;
	}
	m_DatFile << t << '\n';
}

void InputFormat::Print(std::ostream& output)
{
	output << "FORMAT\n";
	output << ",Byte Order," << m_Endian << std::endl;
	output << ",Version," <<  m_Version << std::endl;
	if (m_Version > ORIG_FORMAT_VERSION)
		output << ",zOption," <<  m_ZOption << std::endl;
	output << std::endl;
}

void Dimensions::Validate()
{
	if(m_Units < 0 || m_Units > 1)
		throw SSAMException("Invalid units specified for observation area dimensions.");

	if(m_Scale <= 0)
		throw SSAMException("Invalid (non-positive) scale factor specified for observation area dimensions.");

	if(m_MinX >= m_MaxX)
		throw SSAMException("Dimension record is invalid.  Min X >= Max X (" + std::to_string(m_MinX) + " >= " + std::to_string(m_MaxX) + ").");
	if(m_MinY >= m_MaxY)
		throw SSAMException("Dimension record is invalid.  Min Y >= Max Y (" + std::to_string(m_MinY) + " >= " + std::to_string(m_MaxY) + ").");
}

void Dimensions::Print(std::ostream& output)
{
	output << "DIMENSIONS\n";
	output << ",Units," << m_Units;
	if(m_Units == ENGLISH_UNITS)
		output << ",\"English (i.e., feet, feet/sec, feet/sec2)\"\n";
	else if(m_Units == METRIC_UNITS)
		output << ",\"Metric (i.e., meters, meters/sec, meters/sec2)\"\n";
	else
		output << ",unrecognized";

	output << ",Scale," <<  m_Scale << std::endl;
	output << ",MinX," << m_MinX << std::endl;
	output << ",MinY," << m_MinY << std::endl;
	output << ",MaxX," << m_MaxX << std::endl;
	output << ",MaxY," << m_MaxY << std::endl;
	output << std::endl;			
}

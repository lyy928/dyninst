/*
 * Copyright (c) 1996-2011 Barton P. Miller
 * 
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 * 
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if !defined(INT_PROCESS_H_)
#define INT_PROCESS_H_

#include "proccontrol/h/PCProcess.h"
#include "proccontrol/h/PCErrors.h"
#include "proccontrol/h/Event.h"
#include "proccontrol/h/PlatFeatures.h"

#include "proccontrol/src/response.h"
#include "proccontrol/src/memcache.h"

#include "dynutil/h/dyn_regs.h"
#include "dynutil/h/SymReader.h"
#include "common/h/dthread.h"

#include <vector>
#include <map>
#include <list>
#include <set>
#include <utility>
#include <queue>
#include <stack>

namespace Dyninst {
namespace ProcControlAPI {
class ProcessSet;
class CallStackCallback;
}
}

using namespace Dyninst;
using namespace ProcControlAPI;

class int_thread;
class int_threadPool;
class handlerpool;
class int_iRPC;

typedef std::multimap<Dyninst::Address, Dyninst::ProcControlAPI::Process::ptr> int_addressSet;
typedef std::set<Dyninst::ProcControlAPI::Process::ptr> int_processSet;
typedef std::set<Dyninst::ProcControlAPI::Thread::ptr> int_threadSet;

typedef boost::shared_ptr<int_iRPC> int_iRPC_ptr;
typedef std::map<Dyninst::MachRegister, std::pair<unsigned int, unsigned int> > dynreg_to_user_t;

typedef std::list<int_iRPC_ptr> rpc_list_t;

typedef void* hwbp_key_t;

class bp_instance;
class sw_breakpoint;
class hw_breakpoint;
class int_library;
class int_process;
class emulated_singlestep;

struct bp_install_state {
   Dyninst::Address addr;
   int_breakpoint *bp;
   sw_breakpoint *ibp;
   bool do_install;
   mem_response::ptr mem_resp;
   result_response::ptr res_resp;
};

class mem_state
{
  public:
   typedef mem_state* ptr;
   mem_state(int_process *proc);
   mem_state(mem_state &m, int_process *proc);
   ~mem_state();

   void addProc(int_process *p);
   void rmProc(int_process *p, bool &should_clean);

   std::set<int_process *> procs;
   std::set<int_library *> libs;
   std::map<Dyninst::Address, sw_breakpoint *> breakpoints;
   std::map<Dyninst::Address, unsigned long> inf_malloced_memory;
};

class Counter {
  public:
   static const int NumCounterTypes = 11;
   enum CounterType {
      HandlerRunningThreads = 0,
      GeneratorRunningThreads = 1,
      SyncRPCs = 2,
      SyncRPCRunningThreads = 3,
      PendingStops = 4,
      ClearingBPs = 5,
      ProcStopRPCs = 6,
      AsyncEvents = 7,
      ForceGeneratorBlock = 8,
      GeneratorNonExitedThreads = 9,
      StartupTeardownProcesses = 10
   };

   Counter(CounterType ct_);
   ~Counter();

   void inc();
   void dec();

   bool local() const;
   int localCount() const;
   static bool global(CounterType ct);
   static int globalCount(CounterType ct);
   static int processCount(CounterType ct, int_process* p);

   static const char *getNameForCounter(int counter_type);
  private:
   int local_count;
   CounterType ct;

   void adjust(int val);

   static Mutex locks[NumCounterTypes];
   static int global_counts[NumCounterTypes];
};

class ProcStopEventManager {
  private:
   int_process *proc;
   std::set<Event::ptr> held_pstop_events;
  public:
   ProcStopEventManager(int_process *p);
   ~ProcStopEventManager();

   bool prepEvent(Event::ptr ev);
   void checkEvents();

   bool processStoppedTo(int state_id);
   bool threadStoppedTo(int_thread *thr, int state_id);
};

class int_process
{
   friend class Dyninst::ProcControlAPI::Process;
   friend class Dyninst::ProcControlAPI::ProcessSet;
 protected:
   int_process(Dyninst::PID p, std::string e, std::vector<std::string> a, 
           std::vector<std::string> envp, std::map<int,int> f);
   int_process(Dyninst::PID pid_, int_process *p);
 public:
   static int_process *createProcess(Dyninst::PID p, std::string e);
   static int_process *createProcess(std::string e, std::vector<std::string> a, 
           std::vector<std::string> envp, std::map<int,int> f);
   static int_process *createProcess(Dyninst::PID pid_, int_process *p);
   virtual ~int_process();
 protected:
   static bool create(int_processSet *ps);
   virtual bool plat_create() = 0;
   virtual async_ret_t post_create(std::set<response::ptr> &async_responses);

   static bool attach(int_processSet *ps, bool reattach);
   static bool reattach(int_processSet *pset);
   virtual bool plat_attach(bool allStopped, bool &should_sync) = 0;

   bool attachThreads();
   virtual async_ret_t post_attach(bool wasDetached, std::set<response::ptr> &aresps);
   async_ret_t initializeAddressSpace(std::set<response::ptr> &async_responses);

   virtual bool plat_syncRunState() = 0;
   bool syncRunState();

  public:

   typedef enum {
      ct_fork,
      ct_launch,
      ct_attach
   } creationMode_t;
   creationMode_t getCreationMode() const;

   void setContSignal(int sig);
   int getContSignal() const;
   virtual bool forked();

   virtual OSType getOS() const = 0;
  protected:
   virtual bool plat_forked() = 0;
   virtual bool post_forked();

  public:
   bool execed();
   virtual bool plat_detach(result_response::ptr resp) = 0;
   virtual bool plat_detachDone();
  protected:
   virtual bool plat_execed();
   virtual bool plat_terminate(bool &needs_sync) = 0;

   virtual bool needIndividualThreadAttach() = 0;
   virtual bool getThreadLWPs(std::vector<Dyninst::LWP> &lwps);

   virtual void plat_threadAttachDone();
   bool waitfor_startup();

   void setPid(Dyninst::PID pid);
   int_thread *findStoppedThread();

  public:
   virtual bool plat_processGroupContinues();

   typedef enum {
      neonatal = 0,
      neonatal_intermediate,
      detached,
      running,
      exited,
      errorstate
   } State;
   State getState() const;
   void setState(State s);

   Dyninst::PID getPid() const;
   int_threadPool *threadPool() const;

   Process::ptr proc() const;
   mem_state::ptr memory() const;

   err_t getLastError();
   const char *getLastErrorMsg();
   void clearLastError();
   void setLastError(err_t err, const char *err_str);

   void throwDetachEvent(bool temporary);

   virtual bool preTerminate();
   bool terminate(bool &needs_sync);
   void updateSyncState(Event::ptr ev, bool gen);

   virtual void plat_adjustSyncType(Event::ptr, bool) {};
   virtual Dyninst::Architecture getTargetArch() = 0;
   virtual unsigned getTargetPageSize() = 0;
   virtual unsigned plat_getRecommendedReadSize();
   virtual Dyninst::Address mallocExecMemory(unsigned size);
   virtual Dyninst::Address plat_mallocExecMemory(Dyninst::Address min, unsigned size) = 0;
   virtual void freeExecMemory(Dyninst::Address addr);

   static bool waitAndHandleEvents(bool block);
   static bool waitAndHandleForProc(bool block, int_process *proc, bool &proc_exited);
   static bool waitForAsyncEvent(response::ptr resp);
   static bool waitForAsyncEvent(std::set<response::ptr> resp);

   virtual bool plat_waitAndHandleForProc();

   Counter &asyncEventCount();
   Counter &getForceGeneratorBlockCount();
   Counter &getStartupTeardownProcs();

   static const char *stateName(State s);

   void initializeProcess(Process::ptr p);

   virtual void instantiateRPCThread() {};

   void setExitCode(int c);
   void setCrashSignal(int s);
   bool getExitCode(int &c);
   bool getCrashSignal(int &s);
   bool wasForcedTerminated() const;

   virtual bool plat_individualRegAccess() = 0;
   virtual bool plat_individualRegRead();
   virtual bool plat_individualRegSet();

   int getAddressWidth();
   HandlerPool *handlerPool() const;

   bool addBreakpoint(Dyninst::Address addr, int_breakpoint *bp);
   bool addBreakpoint_phase1(bp_install_state *is);
   bool addBreakpoint_phase2(bp_install_state *is);
   bool addBreakpoint_phase3(bp_install_state *is);

   bool removeBreakpoint(Dyninst::Address addr, int_breakpoint *bp, std::set<response::ptr> &resps);
   sw_breakpoint *getBreakpoint(Dyninst::Address addr);

   virtual unsigned plat_breakpointSize() = 0;
   virtual void plat_breakpointBytes(unsigned char *buffer) = 0;
   virtual bool plat_breakpointAdvancesPC() const = 0;

   virtual bool plat_createDeallocationSnippet(Dyninst::Address addr, unsigned long size, void* &buffer, 
                                               unsigned long &buffer_size, unsigned long &start_offset) = 0;
   virtual bool plat_createAllocationSnippet(Dyninst::Address addr, bool use_addr, unsigned long size, 
                                             void* &buffer, unsigned long &buffer_size, 
                                             unsigned long &start_offset) = 0;
   virtual bool plat_collectAllocationResult(int_thread *thr, reg_response::ptr resp) = 0;
   virtual bool plat_threadOpsNeedProcStop();
   virtual SymbolReaderFactory *plat_defaultSymReader();

   virtual Dyninst::Address direct_infMalloc(unsigned long size, bool use_addr = false, Dyninst::Address addr = 0x0);
   virtual bool direct_infFree(Dyninst::Address addr);

   static bool infMalloc(unsigned long size, int_addressSet *aset, bool use_addr);
   static bool infFree(int_addressSet *aset);

   bool readMem(Dyninst::Address remote, mem_response::ptr result, int_thread *thr = NULL);
   bool writeMem(const void *local, Dyninst::Address remote, size_t size, result_response::ptr result, int_thread *thr = NULL);

   virtual bool plat_readMem(int_thread *thr, void *local, 
                             Dyninst::Address remote, size_t size) = 0;
   virtual bool plat_writeMem(int_thread *thr, const void *local, 
                              Dyninst::Address remote, size_t size) = 0;

   //For a platform, if plat_needsAsyncIO returns true then the async
   // set of functions need to be implemented.  Currently plat_needsAsyncIO
   // only returns true for bluegene family.  By default these are otherwise
   // unimplemented.
   virtual bool plat_needsAsyncIO() const;
   virtual bool plat_readMemAsync(int_thread *thr, Dyninst::Address addr, 
                                  mem_response::ptr result);
   virtual bool plat_writeMemAsync(int_thread *thr, const void *local, Dyninst::Address addr,
                                   size_t size, result_response::ptr result);
   memCache *getMemCache();

   virtual bool plat_getOSRunningStates(std::map<Dyninst::LWP, bool> &runningStates) = 0;
	// Windows-only technically
   virtual void* plat_getDummyThreadHandle() const { return NULL; }
   
   virtual void noteNewDequeuedEvent(Event::ptr ev);

   static bool isInCB();
   static void setInCB(bool b);

   void throwNopEvent();
   void throwRPCPostEvent();
   
   virtual bool plat_getStackInfo(int_thread *thr, stack_response::ptr stk_resp);
   virtual bool plat_handleStackInfo(stack_response::ptr stk_resp, CallStackCallback *cbs);

   virtual bool plat_supportFork();
   virtual bool plat_supportExec();
   virtual bool plat_supportDOTF();

   virtual bool plat_supportThreadEvents();
   virtual bool plat_supportLWPCreate();
   virtual bool plat_supportLWPPreDestroy();
   virtual bool plat_supportLWPPostDestroy();

   virtual bool plat_preHandleEvent();
   virtual bool plat_postHandleEvent();
   virtual bool plat_preAsyncWait();
   virtual bool plat_supportHWBreakpoint();

   virtual bool plat_needsPCSaveBeforeSingleStep();
   virtual async_ret_t plat_needsEmulatedSingleStep(int_thread *thr, std::vector<Dyninst::Address> &result);
   virtual void plat_getEmulatedSingleStepAsyncs(int_thread *thr, std::set<response::ptr> resps);
   virtual bool plat_needsThreadForMemOps() const { return true; }

   int_library *getLibraryByName(std::string s) const;
   size_t numLibs() const;
   virtual bool refresh_libraries(std::set<int_library *> &added_libs,
                                  std::set<int_library *> &rmd_libs,
                                  bool &waiting_for_async,
                                  std::set<response::ptr> &async_responses) = 0;

   virtual bool plat_isStaticBinary() = 0;
   virtual int_library *plat_getExecutable() = 0;

   virtual bool plat_supportDirectAllocation() const { return false; }
   bool forceGeneratorBlock() const;
   void setForceGeneratorBlock(bool b);

   std::string getExecutable() const;
   static bool isInCallback();

   static int_process *in_waitHandleProc;
   // TODO: clean up w/enum
   bool wasCreatedViaAttach() const { return createdViaAttach; }
   void wasCreatedViaAttach(bool val) { createdViaAttach = val; }

   // Platform-specific; is this address in what we consider a system lib.
   virtual bool addrInSystemLib(Address /*addr*/) { return false; }

   ProcStopEventManager &getProcStopManager();

   std::map<int, int> &getProcDesyncdStates();

   bool isRunningSilent(); //No callbacks
   void setRunningSilent(bool b);

   //Interfaces used by the PlatformSpecific classes
   virtual LibraryTracking *sysv_getLibraryTracking();
   virtual bool sysv_setTrackLibraries(bool b, int_breakpoint* &bp, Address &addr, bool &add_bp);
   virtual bool sysv_isTrackingLibraries();

   virtual ThreadTracking *threaddb_getThreadTracking();
   virtual bool threaddb_setTrackThreads(bool b, std::set<std::pair<int_breakpoint *, Address> > &bps,
                                         bool &add_bp);
   virtual bool threaddb_isTrackingThreads();
   virtual bool threaddb_refreshThreads();

   virtual FollowFork *getForkTracking();
   virtual bool fork_setTracking(FollowFork::follow_t b);
   virtual FollowFork::follow_t fork_isTracking();
   
   virtual ExecFileInfo* plat_getExecutableInfo() const { return NULL; }
 protected:
   State state;
   Dyninst::PID pid;
   creationMode_t creation_mode;
   std::string executable;
   std::vector<std::string> argv;
   std::vector<std::string> env;
   std::map<int,int> fds;
   Dyninst::Architecture arch;
   int_threadPool *threadpool;
   Process::ptr up_proc;
   HandlerPool *handlerpool;
   LibraryPool libpool;
   bool hasCrashSignal;
   int crashSignal;
   bool hasExitCode;
   bool forcedTermination;
   bool silent_mode;
   int exitCode;
   static bool in_callback;
   mem_state::ptr mem;
   std::map<Dyninst::Address, unsigned> exec_mem_cache;
   int continueSig;
   bool createdViaAttach;
   memCache mem_cache;
   Counter async_event_count;
   Counter force_generator_block_count;
   Counter startupteardown_procs;
   ProcStopEventManager proc_stop_manager;
   std::map<int, int> proc_desyncd_states;
   FollowFork::follow_t fork_tracking;
   void *user_data;
   err_t last_error;
   const char *last_error_string;
};

struct ProcToIntProc {
   int_process *operator()(const Process::ptr &p) const { return p->llproc(); }
};

class indep_lwp_control_process : virtual public int_process
{
  protected:
   virtual bool plat_syncRunState();
  public:
   indep_lwp_control_process(Dyninst::PID p, std::string e, std::vector<std::string> a, 
                             std::vector<std::string> envp, std::map<int,int> f);
   indep_lwp_control_process(Dyninst::PID pid_, int_process *p);
   virtual ~indep_lwp_control_process();
};

class unified_lwp_control_process : virtual public int_process
{
  protected:
   virtual bool plat_syncRunState();
  public:
   unified_lwp_control_process(Dyninst::PID p, std::string e, std::vector<std::string> a, 
                               std::vector<std::string> envp, std::map<int,int> f);
   unified_lwp_control_process(Dyninst::PID pid_, int_process *p);
   virtual ~unified_lwp_control_process();

   virtual bool plat_processGroupContinues();
};

class hybrid_lwp_control_process : virtual public int_process
{
  protected:
   virtual bool plat_syncRunState();
   virtual bool plat_suspendThread(int_thread *thr) = 0;
   virtual bool plat_resumeThread(int_thread *thr) = 0;
   bool debugger_stopped;
  public:
   hybrid_lwp_control_process(Dyninst::PID p, std::string e, std::vector<std::string> a, 
                              std::vector<std::string> envp, std::map<int,int> f);
   hybrid_lwp_control_process(Dyninst::PID pid_, int_process *p);
   virtual ~hybrid_lwp_control_process();

   virtual bool suspendThread(int_thread *thr);
   virtual bool resumeThread(int_thread *thr);

   virtual void noteNewDequeuedEvent(Event::ptr ev);
   virtual bool plat_debuggerSuspended();

   virtual bool plat_processGroupContinues();
};

class int_registerPool
{
 public:
   int_registerPool();
   int_registerPool(const int_registerPool &c);
   ~int_registerPool();

   typedef std::map<Dyninst::MachRegister, Dyninst::MachRegisterVal> reg_map_t;
   reg_map_t regs;
   bool full;
   int_thread *thread;

   typedef reg_map_t::iterator iterator;
   typedef reg_map_t::const_iterator const_iterator;
};

class thread_exitstate
{
  public:
   Dyninst::LWP lwp;
   Dyninst::THR_ID thr_id;
   Process::ptr proc_ptr;
   void *user_data;
};

class proc_exitstate
{
  public:
   Dyninst::PID pid;
   bool exited;
   bool crashed;
   int crash_signal;
   int exit_code;
   err_t last_error;
   const char *last_error_msg;
   void *user_data;

   void setLastError(err_t e_, const char *m) { last_error = e_; last_error_msg = m; }
};

/**
 * ON THREADING STATES:
 *
 * Each thread has four different states, which mostly monitor running/stopped
 * status :
 *   GeneratorState - Thread state as seen by the generator object
 *   HandlerState - Thread state as seen by the handler object
 *   InternalState - Target thread state desired by int_* layer
 *   UserState - Target Thread state as desired by the user
 *
 * The GeneratorState and HandlerState represent an event as it moves through the 
 * system.  For example, an event that stops the thread may first appear
 * in the Generator object, and move the GeneratorState to 'stopped'.  It is then
 * seen by the handler, which moves the HandlerState to 'stopped'.  If the thread is 
 * then continued, the HandlerState and GeneratorStates will go back to 'running'.
 * These are primarily seperated to prevent race conditions with the handler and
 * generators modifying the same variable, since they run in seperate threads.
 * 
 * The InternalState and UserState are used to make policy decisions about whether
 * a thread should be running or stopped.  For example, after handling an event
 * we may check the UserState to see if the user wants us to run/stop a thread.
 * The InternalState usually matches the UserState, but may override it for 
 * stop/run decisions in a few scenarios.  For example, if the user posts a iRPC
 * to a running process (UserState = running) we may have to temporarily stop the
 * process to install the iRPC.  We don't want to change the UserState, since the user
 * still wants the process running, so we set the InternalState to stopped while we
 * set up the iRPC, and then return it to the UserState value when the iRPC is ready.
 *
 * There are a couple of important assertions about the relationship between these thread
 * states :
 *  (GeneratorState == running) implies (HandlerState == running)
 *  (HandlerState == running)  implies (InternalState == running)
 *  (InternalState == stopped)  implies (HandlerState == stopped)
 *  (HandlerState == stopped)  implies (GeneratorState == stopped)
 **/
class int_thread
{
   friend class Dyninst::ProcControlAPI::Thread;
   friend class int_threadPool;
   friend class ProcStopEventManager;
   friend class hw_breakpoint;
 protected:
   int_thread(int_process *p, Dyninst::THR_ID t, Dyninst::LWP l);
   static int_thread *createThreadPlat(int_process *proc, 
                                       Dyninst::THR_ID thr_id, 
                                       Dyninst::LWP lwp_id,
                                       bool initial_thrd);

public:
   static int_thread *createThread(int_process *proc, 
                                   Dyninst::THR_ID thr_id, 
                                   Dyninst::LWP lwp_id,
   								   bool initial_thrd);
   static int_thread *createRPCThread(int_process *p);
   Process::ptr proc() const;
   int_process *llproc() const;

   Dyninst::LWP getLWP() const;
   void changeLWP(Dyninst::LWP new_lwp);

#define RUNNING_STATE(S) (S == int_thread::running || S == int_thread::neonatal_intermediate)
   typedef enum {
      none=0,
      neonatal=1,
      neonatal_intermediate=2,
      running=3,
      stopped=4,
      dontcare=5,
      ditto=6,
      exited=7,
      detached=8,
      errorstate=9
   } State;
   //The order of these is very important.  Lower numbered
   // states take precedence over higher numbered states.
   static const int NumStateIDs = 16;
   static const int NumTargetStateIDs = (NumStateIDs-2); //Handler and Generator states aren't target states

   static const int AsyncStateID            = 0;
   static const int CallbackStateID         = 1;
   static const int PendingStopStateID      = 2;
   static const int IRPCStateID             = 3;
   static const int IRPCSetupStateID        = 4;
   static const int IRPCWaitStateID         = 5;
   static const int BreakpointStateID       = 6;
   static const int InternalStateID         = 7;
   static const int BreakpointResumeStateID = 8;
   static const int ExitingStateID          = 9;
   static const int StartupStateID          = 10;
   static const int DetachStateID           = 11;
   static const int UserRPCStateID          = 12;
   static const int UserStateID             = 13;
   static const int HandlerStateID          = 14;
   static const int GeneratorStateID        = 15;
   static std::string stateIDToName(int id);

   class StateTracker {
     protected:
      State state;
      int id;
      int sync_level;
      int_thread *up_thr;
     public:
      StateTracker(int_thread *t, int id, int_thread::State initial);

      void desyncState(State ns = int_thread::none);
      void desyncStateProc(State ns = int_thread::none);

      bool setState(State ns = int_thread::none);
      bool setStateProc(State ns = int_thread::none);
      
      void restoreState();
      void restoreStateProc();
      State getState() const;
      bool isDesynced() const;

      std::string getName() const;
      int getID() const;
      
      int_thread *debugthr() const { return up_thr; }
   };

   //State management, see above comment on states
   StateTracker &getExitingState();
   StateTracker &getStartupState();
   StateTracker &getBreakpointState();
   StateTracker &getBreakpointResumeState();
   StateTracker &getCallbackState();
   StateTracker &getIRPCState();
   StateTracker &getIRPCSetupState();
   StateTracker &getIRPCWaitState();
   StateTracker &getAsyncState();
   StateTracker &getInternalState();
   StateTracker &getDetachState();
   StateTracker &getUserRPCState();
   StateTracker &getUserState();
   StateTracker &getHandlerState();
   StateTracker &getGeneratorState();
   StateTracker &getPendingStopState();

   StateTracker &getStateByID(int id);
   StateTracker &getActiveState();
   static char stateLetter(State s);

   Counter &handlerRunningThreadsCount();
   Counter &generatorRunningThreadsCount();
   Counter &syncRPCCount();
   Counter &runningSyncRPCThreadCount();
   Counter &pendingStopsCount();
   Counter &clearingBPCount();
   Counter &procStopRPCCount();
   Counter &getGeneratorNonExitedThreadCount();
      
   //Process control
   bool intStop();
   bool intCont();
   async_ret_t handleSingleStepContinue();

   void terminate();

   void setContSignal(int sig);
   int getContSignal();

   virtual bool plat_cont() = 0;
   virtual bool plat_stop() = 0;
   void setPendingStop(bool b);
   bool hasPendingStop() const;

   bool wasRunningWhenAttached() const;
   void setRunningWhenAttached(bool b);
   bool isStopped(int state_id);

   // Is this thread's lifetime only an IRPC and it gets
   // discarded afterwards?
   virtual bool isRPCEphemeral() const { return false; }

   //Single-step
   bool singleStepMode() const;
   void setSingleStepMode(bool s);
   bool singleStepUserMode() const;
   void setSingleStepUserMode(bool s);
   bool singleStep() const;
   void markClearingBreakpoint(bp_instance *bp);
   bp_instance *isClearingBreakpoint();
   void markStoppedOnBP(bp_instance *bp);
   bp_instance *isStoppedOnBP();

   // Emulating single steps with breakpoints
   void addEmulatedSingleStep(emulated_singlestep *es);
   void rmEmulatedSingleStep(emulated_singlestep *es);
   emulated_singlestep *getEmulatedSingleStep();

   //RPC Management
   void addPostedRPC(int_iRPC_ptr rpc_);
   rpc_list_t *getPostedRPCs();
   bool hasPostedRPCs();
   void setRunningRPC(int_iRPC_ptr rpc_);
   void clearRunningRPC();
   int_iRPC_ptr runningRPC() const;
   bool saveRegsForRPC(allreg_response::ptr response);
   bool restoreRegsForRPC(bool clear, result_response::ptr response);
   bool hasSavedRPCRegs();
   void incSyncRPCCount();
   void decSyncRPCCount();
   bool hasSyncRPC();
   int_iRPC_ptr nextPostedIRPC() const;

   int_iRPC_ptr hasRunningProcStopperRPC() const;
   virtual bool notAvailableForRPC() {
		return false;
   }

   //Register Management
   bool getAllRegisters(allreg_response::ptr result);
   bool getRegister(Dyninst::MachRegister reg, reg_response::ptr result);
   bool setAllRegisters(int_registerPool &pool, result_response::ptr result);
   bool setRegister(Dyninst::MachRegister reg, Dyninst::MachRegisterVal val, result_response::ptr result);

   virtual bool plat_getAllRegisters(int_registerPool &pool) = 0;
   virtual bool plat_getRegister(Dyninst::MachRegister reg, 
                                 Dyninst::MachRegisterVal &val) = 0;
   virtual bool plat_setAllRegisters(int_registerPool &pool) = 0;
   virtual bool plat_setRegister(Dyninst::MachRegister reg, 
                                 Dyninst::MachRegisterVal val) = 0;

   virtual bool plat_getAllRegistersAsync(allreg_response::ptr result);
   virtual bool plat_getRegisterAsync(Dyninst::MachRegister reg, 
                                      reg_response::ptr result);
   virtual bool plat_setAllRegistersAsync(int_registerPool &pool,
                                          result_response::ptr result);
   virtual bool plat_setRegisterAsync(Dyninst::MachRegister reg, 
                                      Dyninst::MachRegisterVal val,
                                      result_response::ptr result);
   virtual void plat_terminate();

   void updateRegCache(int_registerPool &pool);
   void updateRegCache(Dyninst::MachRegister reg, Dyninst::MachRegisterVal val);
   void clearRegCache();

   // The exiting property is separate from the main state because an
   // exiting thread can either be running or stopped (depending on the
   // desires of the user).
   bool isExiting() const;
   void setExiting(bool b);
   bool isExitingInGenerator() const;
   void setExitingInGenerator(bool b);

   static void cleanFromHandler(int_thread *thr, bool should_delete);

   //Misc
   virtual bool attach() = 0;
   Thread::ptr thread();

   typedef void(*continue_cb_t)(int_thread *thrd);
   static void addContinueCB(continue_cb_t cb);
   void triggerContinueCBs();

   void throwEventsBeforeContinue();

   //User level thread info
   void setTID(Dyninst::THR_ID tid_);
   virtual bool haveUserThreadInfo();
   virtual bool getTID(Dyninst::THR_ID &tid);
   virtual bool getStartFuncAddress(Dyninst::Address &addr);
   virtual bool getStackBase(Dyninst::Address &addr);
   virtual bool getStackSize(unsigned long &size);
   virtual bool getTLSPtr(Dyninst::Address &addr);
      
   // Windows-only; default implementation is "yes, we're a user thread"
   virtual bool isUser() const { return true; }

   virtual unsigned hwBPAvail(unsigned mode);
   virtual bool rmHWBreakpoint(hw_breakpoint *bp,
                               bool suspend,
                               std::set<response::ptr> &resps,
                               bool &done);
   virtual bool addHWBreakpoint(hw_breakpoint *bp,
                                bool resume,
                                std::set<response::ptr> &resps,
                                bool &done);
   virtual EventBreakpoint::ptr decodeHWBreakpoint(response::ptr &resp,
                                                   bool have_reg = false,
                                                   Dyninst::MachRegisterVal regval = 0);
   virtual bool bpNeedsClear(hw_breakpoint *hwbp);

   virtual ~int_thread();
   static const char *stateStr(int_thread::State s);

   State getTargetState() const;
   void setTargetState(State s);

   void setSuspended(bool b);
   bool isSuspended() const;
   void setLastError(err_t ec, const char *es);

   hw_breakpoint *getHWBreakpoint(Address addr);
 protected:
   Dyninst::THR_ID tid;
   Dyninst::LWP lwp;
   int_process *proc_;
   Thread::ptr up_thread;
   int continueSig_;

   Counter handler_running_thrd_count;
   Counter generator_running_thrd_count;
   Counter sync_rpc_count;
   Counter sync_rpc_running_thr_count;
   Counter pending_stop;
   Counter clearing_bp_count;
   Counter proc_stop_rpc_count;
   Counter generator_nonexited_thrd_count;

   StateTracker exiting_state;
   StateTracker startup_state;
   StateTracker pending_stop_state;
   StateTracker callback_state;
   StateTracker breakpoint_state;
   StateTracker breakpoint_resume_state;
   StateTracker irpc_setup_state;
   StateTracker irpc_wait_state;
   StateTracker irpc_state;
   StateTracker async_state;
   StateTracker internal_state;
   StateTracker detach_state;
   StateTracker user_irpc_state;
   StateTracker user_state;
   StateTracker handler_state;
   StateTracker generator_state;
   StateTracker *all_states[NumStateIDs];

   State target_state;
   State saved_user_state;

   int_registerPool cached_regpool;
   Mutex regpool_lock;
   int_iRPC_ptr running_rpc;
   int_iRPC_ptr writing_rpc;
   rpc_list_t posted_rpcs;
   int_registerPool rpc_regs;

   bool user_single_step;
   bool single_step;
   bool handler_exiting_state;
   bool generator_exiting_state;
   bool running_when_attached;
   bool suspended;

   Address stopped_on_breakpoint_addr;
   Address postponed_stopped_on_breakpoint_addr;

   bp_instance *clearing_breakpoint;
   emulated_singlestep *em_singlestep;
   void *user_data;

   std::set<hw_breakpoint *> hw_breakpoints;
   static std::set<continue_cb_t> continue_cbs;
};

class int_threadPool {
   friend class Dyninst::ProcControlAPI::ThreadPool;
   friend class Dyninst::ProcControlAPI::ThreadPool::iterator;
   friend class int_thread;
 private:
   std::vector<int_thread *> threads;
   std::vector<Thread::ptr> hl_threads;
   std::map<Dyninst::LWP, int_thread *> thrds_by_lwp;

   mutable int_thread *initial_thread; // may be updated by side effect on Windows
   int_process *proc_;
   ThreadPool *up_pool;
   bool had_multiple_threads;
 public:
   int_threadPool(int_process *p);
   ~int_threadPool();

   void setInitialThread(int_thread *thrd);
   void addThread(int_thread *thrd);
   void rmThread(int_thread *thrd);
   void noteUpdatedLWP(int_thread *thrd);
   void clear();
   bool hadMultipleThreads() const;

   typedef std::vector<int_thread *>::iterator iterator;
   iterator begin() { return threads.begin(); }
   iterator end() { return threads.end(); }
   bool empty() { return threads.empty(); }

   unsigned size() const;

   int_process *proc() const;
   ThreadPool *pool() const;

   int_thread *findThreadByLWP(Dyninst::LWP lwp);
   int_thread *initialThread() const;
   bool allHandlerStopped();
   bool allStopped(int state_id);
   
   void saveUserState(Event::ptr ev);
   void restoreUserState();
};

class int_library
{
   friend class Dyninst::ProcControlAPI::LibraryPool;
   friend class Dyninst::ProcControlAPI::LibraryPool::iterator;
   friend class Dyninst::ProcControlAPI::LibraryPool::const_iterator;
  private:
   std::string name;
   Dyninst::Address load_address;
   Dyninst::Address data_load_address;
   Dyninst::Address dynamic_address;
   bool has_data_load;
   bool marked;
   void *user_data;
   Library::ptr up_lib;
  public:
   int_library(std::string n, 
               Dyninst::Address load_addr,
               Dyninst::Address dynamic_load_addr, 
               Dyninst::Address data_load_addr = 0, 
               bool has_data_load_addr = false);
   int_library(int_library *l);
   ~int_library();
   std::string getName();
   Dyninst::Address getAddr();
   Dyninst::Address getDataAddr();
   Dyninst::Address getDynamicAddr();
   bool hasDataAddr();
   
   void setMark(bool b);
   bool isMarked() const;
   
   void setUserData(void *d);
   void *getUserData();

   Library::ptr getUpPtr() const;
   void markAsCleanable();
};

class int_breakpoint
{
   friend class sw_breakpoint;
   friend class hw_breakpoint;
 private:
   Breakpoint::weak_ptr up_bp;
   Dyninst::Address to;
   bool isCtrlTransfer_;
   void *data;

   bool hw;
   unsigned int hw_perms;
   unsigned int hw_size;
   bool onetime_bp;
   bool onetime_bp_hit;
   bool procstopper;
   bool suppress_callbacks;
   std::set<Thread::const_ptr> thread_specific;
 public:
   int_breakpoint(Breakpoint::ptr up);
   int_breakpoint(Dyninst::Address to, Breakpoint::ptr up);
   int_breakpoint(unsigned int hw_prems_, unsigned int hw_size_, Breakpoint::ptr up);
   ~int_breakpoint();

   bool isCtrlTransfer() const;
   Dyninst::Address toAddr() const;
   Dyninst::Address getAddress(int_process *p) const;
   void *getData() const;
   void setData(void *v);
   
   void setOneTimeBreakpoint(bool b);
   void markOneTimeHit();
   bool isOneTimeBreakpoint() const;
   bool isOneTimeBreakpointHit() const;

   void setThreadSpecific(Thread::const_ptr p);
   bool isThreadSpecific() const;
   bool isThreadSpecificTo(Thread::const_ptr p) const;

   void setProcessStopper(bool b);
   bool isProcessStopper() const;

   void setSuppressCallbacks(bool);
   bool suppressCallbacks(void) const;
   
   bool isHW() const;
   unsigned getHWSize() const;
   unsigned getHWPerms() const;
   
   Breakpoint::weak_ptr upBreakpoint() const;
};

class bp_instance
{
   friend class Dyninst::ProcControlAPI::EventBreakpoint;
  protected:
   std::set<int_breakpoint *> bps;
   std::set<Breakpoint::ptr> hl_bps;
   Dyninst::Address addr;
   bool installed;
   int suspend_count;
   
   sw_breakpoint *swbp;
   hw_breakpoint *hwbp;

   bool suspend_common();
   bool resume_common();
  public:
   virtual bool checkBreakpoint(int_breakpoint *bp, int_process *proc);
   virtual bool rmBreakpoint(int_process *proc, int_breakpoint *bp,
                             bool &empty, std::set<response::ptr> &resps);
   virtual async_ret_t uninstall(int_process *proc, std::set<response::ptr> &resps) = 0;

   Address getAddr() const;

   bp_instance(Address addr);
   bp_instance(const bp_instance *ip);

   typedef std::set<int_breakpoint *>::iterator iterator;
   iterator begin();
   iterator end();

   bool containsIntBreakpoint(int_breakpoint *bp);
   int_breakpoint *getCtrlTransferBP(int_thread *thread);

   bool isInstalled() const;
   virtual bool needsClear() = 0;

   virtual async_ret_t suspend(int_process *proc, std::set<response::ptr> &resps) = 0;
   virtual async_ret_t resume(int_process *proc, std::set<response::ptr> &resps) = 0;

   sw_breakpoint *swBP();
   hw_breakpoint *hwBP();
   virtual ~bp_instance();
};
 

//At least as large as any arch's trap instruction
#define BP_BUFFER_SIZE 8
//Long breakpoints can be used to artifically increase the size of the BP write,
// which fools the BG breakpoint interception code that looks for 4 byte writes.
#define BP_LONG_SIZE 4
class sw_breakpoint : public bp_instance
{
   friend class Dyninst::ProcControlAPI::EventBreakpoint;
   friend class int_process;
 private:
   mem_state::ptr memory;

   char buffer[BP_BUFFER_SIZE];
   int buffer_size;
   bool prepped;
   bool long_breakpoint;

   result_response::ptr write_response;
   mem_response::ptr read_response;

   bool writeBreakpoint(int_process *proc, result_response::ptr write_response);
   bool saveBreakpointData(int_process *proc, mem_response::ptr read_response);
   bool restoreBreakpointData(int_process *proc, result_response::ptr res_resp);

   sw_breakpoint(mem_state::ptr memory_, Dyninst::Address addr_);
 public:
   sw_breakpoint(mem_state::ptr memory_, const sw_breakpoint *ip);
   ~sw_breakpoint();

   static sw_breakpoint *create(int_process *proc, int_breakpoint *bp, Dyninst::Address addr_);
   //Use these three functions to add a breakpoint
   bool prepBreakpoint(int_process *proc, mem_response::ptr mem_resp);
   bool insertBreakpoint(int_process *proc, result_response::ptr res_resp);
   bool addToIntBreakpoint(int_breakpoint *bp, int_process *proc);

   virtual async_ret_t uninstall(int_process *proc, std::set<response::ptr> &resps);
   virtual async_ret_t suspend(int_process *proc, std::set<response::ptr> &resps);
   virtual async_ret_t resume(int_process *proc, std::set<response::ptr> &resps);

   unsigned getNumIntBreakpoints() const;
   virtual bool needsClear();
};

class hw_breakpoint : public bp_instance {
  friend class Dyninst::ProcControlAPI::EventBreakpoint;
private:
  unsigned int hw_perms;
  unsigned int hw_size;
  bool proc_wide;
  int_thread *thr;
  bool error;

  hw_breakpoint(int_thread *thr, unsigned mode, unsigned size,
                bool pwide, Dyninst::Address addr);
public:
  virtual async_ret_t uninstall(int_process *proc, std::set<response::ptr> &resps);


  static hw_breakpoint *create(int_process *proc, int_breakpoint *bp, Dyninst::Address addr_);
  ~hw_breakpoint();
  
  bool install(bool &done, std::set<response::ptr> &resps);
  unsigned int getPerms() const;
  unsigned int getSize() const;
  bool procWide() const;
  int_thread *getThread() const;
  virtual bool needsClear();

  virtual async_ret_t suspend(int_process *proc, std::set<response::ptr> &resps);
  virtual async_ret_t resume(int_process *proc, std::set<response::ptr> &resps);
};

class emulated_singlestep {
   // Breakpoints that are added and removed in a group to emulate
   // a single step with breakpoints
  private:
   bool saved_user_single_step;
   bool saved_single_step;
   int_breakpoint *bp;
   int_thread *thr;
   std::set<Address> addrs;

  public:
   emulated_singlestep(int_thread *thr);
   ~emulated_singlestep();
   
   bool containsBreakpoint(Address addr) const;
   async_ret_t add(Address addr);
   async_ret_t clear();
   void restoreSSMode();

   std::set<response::ptr> clear_resps;
};

struct clearError {
   void operator()(Process::ptr p) {
      p->clearLastError();
   }

   template <class T>
   void operator()(const std::pair<T, Process::const_ptr> &v) {
      v.second->clearLastError();
   }
   template <class T>
   void operator()(const std::pair<Process::const_ptr, T> &v) {
      v.first->clearLastError();
   }
   template <class T>
   void operator()(const std::pair<T, Process::ptr> &v) {
      v.second->clearLastError();
   }
   template <class T>
   void operator()(const std::pair<Process::ptr, T> &v) {
      v.first->clearLastError();
   }
};

struct setError {
private:
   err_t err;
   const char *err_str;
public:
   setError(err_t e, const char *s) { err = e; err_str = s; }
   void operator()(Process::ptr p) {
      p->setLastError(err, err_str);
   }
   void operator()(const std::pair<Address, Process::ptr> &v) {
      v.second->setLastError(err, err_str);
   }
};

class int_notify {
#if defined(os_windows)
	class windows_details
	{
		HANDLE evt;
	public:
		windows_details() : evt(INVALID_HANDLE_VALUE)
		{
		}
		typedef HANDLE wait_object_t;
		void noteEvent()
		{
			::ReleaseSemaphore(evt, 1, NULL);
		}
		void clearEvent()
		{
			// No-op with semaphores
			//::ResetEvent(evt);
		}

		bool createInternals()
		{
			evt = ::CreateSemaphore(NULL, 0, 1000, NULL);
			return evt  != INVALID_HANDLE_VALUE;
		}
		bool internalsValid()
		{
			return evt != INVALID_HANDLE_VALUE;
		}
		wait_object_t getWaitObject()
		{
			return evt;
		}
	};
	typedef windows_details details_t;
#else
	class unix_details
	{
		friend class int_notify;
		int pipe_in;
		int pipe_out;
		void writeToPipe();
		void readFromPipe();
	public:
		unix_details();
		typedef int wait_object_t;
		void noteEvent();
		void clearEvent();
		bool createInternals();
		bool internalsValid();
		wait_object_t getWaitObject();
	};
	typedef unix_details details_t;
#endif

	typedef details_t::wait_object_t wait_object_t;
   friend int_notify *notify();
   friend EventNotify *Dyninst::ProcControlAPI::evNotify();
 private:
   static int_notify *the_notify;
   EventNotify *up_notify;
   std::set<EventNotify::notify_cb_t> cbs;
   int events_noted;
   details_t my_internals;
 public:
   int_notify();
   void noteEvent();
   void clearEvent();
   
   void registerCB(EventNotify::notify_cb_t cb);
   void removeCB(EventNotify::notify_cb_t cb);
   bool hasEvents();
   details_t::wait_object_t getWaitable();
};
int_notify *notify();

extern void setGeneratorThread(long t);
void setHandlerThread(long t);
bool isGeneratorThread();
bool isHandlerThread();
bool isUserThread();
HandlerPool *createDefaultHandlerPool(int_process *p);
HandlerPool *plat_createDefaultHandlerPool(HandlerPool *hpool);

class MTManager {
  friend MTManager *mt();
  friend class MTLock;
private:
  static MTManager *mt_;
  DThread evhandler_thread;
  CondVar pending_event_lock;
  Mutex work_lock;
  bool have_queued_events;
  bool is_running;
  bool should_exit;
  Process::thread_mode_t threadMode;
  
  void evhandler_main();
#if defined(os_windows)
  static unsigned long WINAPI evhandler_main_wrapper(void *);
#else
  static void evhandler_main_wrapper(void *);
#endif
  void eventqueue_cb();
  
public:
  static const Process::thread_mode_t default_thread_mode = Process::HandlerThreading;
  MTManager();
  ~MTManager();
  
  void run();
  void stop();

  void startWork();
  void endWork();
  
  bool handlerThreading();
  Process::thread_mode_t getThreadMode();
  bool setThreadMode(Process::thread_mode_t tm, bool init = false);

  static void eventqueue_cb_wrapper();
}; 

inline MTManager *mt() { 
   return MTManager::mt_; 
}

class MTLock
{
 private:
   bool should_unlock;
 public:
   typedef enum {allow_init} initialize;
   typedef enum {allow_generator} generator;
   typedef enum {nocb, deliver_callbacks} callbacks;
   MTLock(initialize, callbacks c = nocb)
   {
      should_unlock = true;
      if (!MTManager::mt_) {
         MTManager::mt_ = new MTManager();
         if ((MTManager::default_thread_mode == Process::HandlerThreading) ||
             (MTManager::default_thread_mode == Process::CallbackThreading)) {
            mt()->startWork();
         }
         mt()->setThreadMode(MTManager::default_thread_mode, true);
      }
      else if (mt()->handlerThreading()) {
         mt()->startWork();
         if ((c == deliver_callbacks) && 
             (MTManager::default_thread_mode == Process::HandlerThreading) && 
             notify()->hasEvents()) 
         {
            pthrd_printf("MTLock triggered event handling\n");
            int_process::waitAndHandleEvents(false);
            pthrd_printf("MTLock triggered event handling finished\n");
         }
      }
   }

   MTLock(generator)
   {
      if (isGeneratorThread()) {
         should_unlock = false;
         return;
      }
      should_unlock = true;
      if (mt()->handlerThreading()) {
         mt()->startWork();
      }
   }

   MTLock(callbacks) 
   {
      assert(!isGeneratorThread());
      should_unlock = true;
      if (mt()->handlerThreading()) {
         mt()->startWork();
         if (notify()->hasEvents() && 
             (MTManager::default_thread_mode == Process::HandlerThreading) )
         {
            pthrd_printf("MTLock triggered event handling\n");
            int_process::waitAndHandleEvents(false);
            pthrd_printf("MTLock triggered event handling finished\n");
         }
      }
   }

   MTLock()
   {
      assert(!isGeneratorThread());
      should_unlock = true;
      if (mt()->handlerThreading())
         mt()->startWork();
   }

   ~MTLock() {
      if (should_unlock && mt()->handlerThreading())
         mt()->endWork();
   }
};

// A class to stop the various threads that have been started when
// the library is deinitialized
class int_cleanup {
    public:
        ~int_cleanup();
};

#endif

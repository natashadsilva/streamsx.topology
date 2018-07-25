/*
# Licensed Materials - Property of IBM
# Copyright IBM Corp. 2015,2016
*/

/*
 * Internal header file supporting Python
 * for com.ibm.streamsx.topology.
 *
 * This is not part of any public api for
 * the toolkit or toolkit with decorated
 * SPL Python operators.
 *
 * Functionality related to operators
 */

#ifndef __SPL__SPLPY_OP_H
#define __SPL__SPLPY_OP_H

#include <SPL/Runtime/Operator/OperatorMetrics.h>
#include <SPL/Runtime/Common/Metric.h>
#include <SPL/Runtime/Operator/OptionalContext.h>
#include <SPL/Runtime/Operator/State/ConsistentRegionContext.h>
#include <SPL/Runtime/Operator/State/StateHandler.h>

#include "splpy_ec_api.h"
#include "splpy_general.h"
#include "splpy_setup.h"

namespace streamsx {
  namespace topology {

using UTILS_NAMESPACE_QUALIFIER AutoMutex;
using UTILS_NAMESPACE_QUALIFIER Mutex;

class SplpyOp;

/**
 * State handler interface definition, and do-nothing
 * base class.
 */
class SplpyOpStateHandler : public SPL::StateHandler {
private:
  virtual Mutex * getMutex() { return NULL; }
  friend class SplpyOp;
};

/**
 * Support for saving an operator's state to checkpoints, and restoring
 * the state from checkpoints.
 */
class SplpyOpStateHandlerImpl : public SplpyOpStateHandler {
 public:
  SplpyOpStateHandlerImpl(SplpyOp * pyop, PyObject * pickledCallable);
  virtual ~SplpyOpStateHandlerImpl();
  virtual void checkpoint(SPL::Checkpoint & ckpt);
  virtual void reset(SPL::Checkpoint & ckpt);
  virtual void resetToInitialState();
 private:
  virtual Mutex* getMutex();
  static PyObject * call(PyObject * callable, PyObject * arg);
  SplpyOp * op;
  PyObject * loads;
  PyObject * dumps;
  PyObject * pickledInitialCallable;
  Mutex mutex_;
};

class SplpyOp {
  public:
      SplpyOp(SPL::Operator *op, const char * spl_setup_py) :
          op_(op),
          callable_(NULL),
          pydl_(NULL),
          exc_suppresses(NULL),
#if __SPLPY_EC_MODULE_OK
          opc_(NULL),
#endif
          stateHandler(NULL),
          stateHandlerMutex(NULL),
          consistentRegionContext(NULL)
      {
          pydl_ = SplpySetup::loadCPython(spl_setup_py);

          SplpyGIL lock;
          SPL::rstring outDir(op->getPE().getOutputDirectory());
          PyObject * pyOutDir = pySplValueToPyObject(outDir);
          SplpyGeneral::callVoidFunction(
               "streamsx.topology.runtime", "add_output_packages", pyOutDir, NULL);
#if __SPLPY_EC_MODULE_OK
          opc_ = PyLong_FromVoidPtr((void*)op);
          if (opc_ == NULL)
              throw SplpyGeneral::pythonException("capsule");
#endif
          consistentRegionContext = static_cast<SPL::ConsistentRegionContext *>
            (op->getContext().getOptionalContext(CONSISTENT_REGION));
          
      }

      virtual ~SplpyOp()
      {
        {
          SplpyGIL lock;

          if (callable_ != NULL)
              Py_DECREF(callable_);

#if __SPLPY_EC_MODULE_OK
          if (opc_ != NULL)
              Py_DECREF(opc_);
#endif
        }
        if (pydl_ != NULL)
          (void) dlclose(pydl_);

        delete stateHandler;
        stateHandler = NULL;
        stateHandlerMutex = NULL; // not owned by this class
      }

      SPL::Operator * op() {
         return op_;
      }

      /**
       * Get the consistent region context for the operator,
       * or NULL if the operator is not part of a consistent region.
       */
      SPL::ConsistentRegionContext * getConsistentRegionContext() {
        return consistentRegionContext;
      }

      void setCallable(PyObject * callable) {
        bool firstTime = (callable_ == NULL);
        callable_ = callable;
        if (firstTime) {
          setup();
        }
      }
      PyObject * callable() {
          return callable_;
      }

      /**
       * perform any common setup for a Python callable.
       * 
       * - If the callable as an __enter__/__exit__ pair
       *   (__enter__ will have already been called) then create
       *   a metric that keeps track of exceptions suppressed
       *   by __exit__
       */
      void setup() {
          if (PyObject_HasAttrString(callable_, "_splpy_entered")) {
              PyObject *entered = PyObject_GetAttrString(callable_, "_splpy_entered");
              if (PyObject_IsTrue(entered)) {
                  SPL::OperatorMetrics & metrics = op_->getContext().getMetrics();
                  SPL::Metric &cm = metrics.createCustomMetric(
                      "nExceptionsSuppressed",
                      "Number of exceptions suppressed by callable's __exit__ method.",
                      SPL::Metric::Counter);
                  exc_suppresses = &cm;
              }
              Py_DECREF(entered);
          }

          setupStateHandler();
      }

      /**
       * Actions for a Python operator on prepareToShutdown
       * Flush any pending output.
      */
      void prepareToShutdown() {
          SplpyGIL lock;
          if (callable_) {
             // Call _shutdown_op which will invoke
             // __exit__ on the users object if
             // it's a class instance and has
             // __enter__ and __exit__
             // callVoid steals the reference to callable_
             Py_INCREF(callable_);
             SplpyGeneral::callVoidFunction(
               "streamsx.ec", "_shutdown_op", callable_, NULL);
          }
          SplpyGeneral::flush_PyErrPyOut();
      }

      int exceptionRaised(const SplpyExceptionInfo& exInfo) {
          if (callable_) {
             // callFunction steals the reference to callable_
             Py_INCREF(callable_);
             PyObject *ignore = SplpyGeneral::callFunction(
               "streamsx.ec", "_shutdown_op", callable_, exInfo.asTuple());
             int ignoreException = PyObject_IsTrue(ignore);
             Py_DECREF(ignore);
             if (ignoreException && exc_suppresses)
                 exc_suppresses->incrementValue();
             return ignoreException;
          }
          return 0;
      }

#if __SPLPY_EC_MODULE_OK
      // Get the capture with a new ref
      PyObject * opc() {
         Py_INCREF(opc_);
         return opc_;
      }
     
      // Set the operator capsule as a Python thread local
      // use streamsx.ec._set_opc so that it is availble
      // through the operator's class __init__ function.
      void setopc() {
         SplpyGeneral::callVoidFunction(
               "streamsx.ec", "_set_opc", opc(), NULL);
      }

      // Clear the thread local for the operator capsule
      void clearopc() {
          SplpyGeneral::callVoidFunction(
               "streamsx.ec", "_clear_opc",
               NULL, NULL);
      }
#endif

      /**
       * Is this operator stateful for checkpointing?  Derived classes
       * must override this to support checkpointing.
       */
      virtual bool isStateful () { return false; }

      /**
       * Register a state handler for the operator.  The state handler
       * handles checkpointing and supports consistent regions.  Checkpointing
       * will be enabled for this operator only if checkpoint is enabled for
       * the topology, this operator is stateful, and it can be saved and
       * restored using dill.
       */
      virtual void setupStateHandler() {
        // If the checkpointing or consistent region is enabled and the 
        // operator is stateful, create and register a state handler instance. 
        // Otherwise, register a do-nothing state handler.
        bool stateful = isStateful();
        bool checkpointing = op()->getContext().isCheckpointingOn();
        if (checkpointing) 
          SPLAPPTRC(L_TRACE, "checkpointing enabled", "python");
        else 
          SPLAPPTRC(L_TRACE, "checkpointing disabled", "python");
        bool consistentRegion = (NULL != op()->getContext().getOptionalContext(CONSISTENT_REGION));
        if (consistentRegion)
          SPLAPPTRC(L_TRACE, "consistent region enabled", "python");
        else 
          SPLAPPTRC(L_TRACE, "consistent region disabled", "python");
        // TODO what if both are enabled?

        if (checkpointing || consistentRegion) {
          PyObject * pickledCallable = NULL;
          if (stateful) {

            // Ensure that callable() can be pickled before using it in a state
            // handler.
            SplpyGIL lock;
            PyObject * dumps = SplpyGeneral::loadFunction("dill", "dumps");
            PyObject * args = PyTuple_New(1);
            Py_INCREF(callable());
            PyTuple_SET_ITEM(args, 0, callable());
            pickledCallable = PyObject_CallObject(dumps, args);
            Py_DECREF(args);
            Py_DECREF(dumps);

            std::stringstream msg;
            msg << "Checkpointing is not available for the " << op()->getContext().getName() << " operator";

            if (!pickledCallable) {
              // The callable cannot be pickled.  Throw an exception that
              // shuts down the operator, unless it is supressed.
              // If it is suppressed, this operator will continue to run, but
              // with no checkpointing enabled.
              if (PyErr_Occurred()) {
                SplpyExceptionInfo exceptionInfo = SplpyExceptionInfo::pythonError("setup");
                if (exceptionInfo.pyValue_) {
                  SPL::rstring text;
                  // note pyRStringFromPyObject returns zero on success
                  if (pyRStringFromPyObject(text, exceptionInfo.pyValue_) == 0) {
                    msg << " because of python error " << text;
                  }

                  SPLAPPTRC(L_WARN, msg.str(), "python");
                  // Offer the exception to the operator, so the operator
                  // can suppress it.
                  if (exceptionRaised(exceptionInfo) == 0) {
                    throw exceptionInfo.exception();
                  }
                  exceptionInfo.clear();
                  // The exception was suppressed.  Continue with
                  // pickledCallable == NULL, which will cause a do-nothing
                  // state handler to be created.
                  // Effectively, checkpointing will not be enabled for
                  // this operator even though it is stateful.
                  SPLAPPTRC(L_WARN, "Proceeding with no checkpointing for the " << op()->getContext().getName() << " operator", "python");
                }
              }
              else {
                // This is probably unreachable.  PyObject_CallObject
                // returned NULL, but there was no python exception.
                throw SplpyGeneral::generalException("setup", msg.str());
              }
            }
          }
          assert(!stateHandler);
          if (stateful && pickledCallable) {
            SPLAPPTRC(L_DEBUG, "Creating functional state handler", "python");
            // pickledCallable reference stolen here.
            stateHandler = new SplpyOpStateHandlerImpl(this, pickledCallable);
            stateHandlerMutex = stateHandler->getMutex();
          }
          else {
            SPLAPPTRC(L_DEBUG, "Creating nonfunctional state handler", "python");
            stateHandler = new SplpyOpStateHandler;
            stateHandlerMutex = NULL;
          }
          // TODO this should be removed.
          SPLAPPTRC(L_DEBUG, "registerStateHandler", "python");
          op()->getContext().registerStateHandler(*stateHandler);
        }
      }

      void checkpoint(SPL::Checkpoint & ckpt) {
        if (stateHandler) {
          stateHandler->checkpoint(ckpt);
        }
      }
      
      void reset(SPL::Checkpoint & ckpt) {
        if (stateHandler) {
          stateHandler->reset(ckpt);
        }
      }

      void resetToInitialState() {
        if (stateHandler) {
          stateHandler->resetToInitialState();
        }
      }

      // TODO this can go now.
      class RealAutoLock {
      public:
        RealAutoLock(SplpyOp * op) : op_(op) {
          locked_ = (op->stateHandlerMutex != NULL);
          if (locked_){
            op->stateHandlerMutex->lock();
          }
        }
        ~RealAutoLock() {
          unlock();
        }
        void unlock() {
          if (locked_) {
            op_->stateHandlerMutex->unlock();
            locked_ = false;
          }
        }

      private:
        RealAutoLock(RealAutoLock const & other);
        RealAutoLock();

        bool locked_;
        SplpyOp * op_;
      };
      friend class RealAutoLock;

      class NoAutoLock {
      public:
        NoAutoLock(SplpyOp *) {}
        void unlock() {}
      };

   private:
      SPL::Operator *op_;

      // Python object used to process tuples
      PyObject *callable_;

      // Handle to libpythonX.Y.so
      void * pydl_;

      // Number of exceptions suppressed by __exit__
      SPL::Metric *exc_suppresses;

#if __SPLPY_EC_MODULE_OK
      // PyLong of op_
      PyObject *opc_;
#endif

      SplpyOpStateHandler * stateHandler;
      Mutex * stateHandlerMutex;
      SPL::ConsistentRegionContext * consistentRegionContext;
};

 // Steals reference to pickledCallable
 SplpyOpStateHandlerImpl::SplpyOpStateHandlerImpl(SplpyOp * pyop, PyObject * pickledCallable) : op(pyop), loads(), dumps(), pickledInitialCallable(pickledCallable), mutex_() {
  // Load pickle.loads and pickle.dumps
  SplpyGIL lock;
  loads = SplpyGeneral::loadFunction("dill", "loads");
  dumps = SplpyGeneral::loadFunction("dill", "dumps");
 }

 SplpyOpStateHandlerImpl::~SplpyOpStateHandlerImpl() {
   SplpyGIL lock;
   Py_CLEAR(loads);
   Py_CLEAR(dumps);
   Py_CLEAR(pickledInitialCallable);
 }

 void SplpyOpStateHandlerImpl::checkpoint(SPL::Checkpoint & ckpt) {
   SPLAPPTRC(L_DEBUG, "checkpoint", "python");
   AutoMutex am(mutex_);
   SPL::blob bytes;
   {
     SplpyGIL lock;
     PyObject * ret = call(dumps, op->callable());
     if (!ret) {
       SplpyGeneral::tracePythonError();
       throw SplpyGeneral::pythonException("dill.dumps");
     }
     pySplValueFromPyObject(bytes, ret);
     Py_DECREF(ret);
   }
   ckpt << bytes;
   SPLAPPTRC(L_TRACE, "exit checkpoint", "python");
 }

 void SplpyOpStateHandlerImpl::reset(SPL::Checkpoint & ckpt) {
   SPLAPPTRC(L_DEBUG, "reset", "python");
   AutoMutex am(mutex_);
   // Restore the callable from an spl blob
   SPL::blob bytes;
   ckpt >> bytes;
   SplpyGIL lock;
   PyObject * pickle = pySplValueToPyObject(bytes);
   PyObject * ret = call(loads, pickle);
   if (!ret) {
       SplpyGeneral::tracePythonError();
       throw SplpyGeneral::pythonException("dill.loads");
   }
   // discard the old callable, replace with the newly
   // unpickled one.
   Py_DECREF(op->callable());
   op->setCallable(ret); // reference to ret stolen by op
 }

 void SplpyOpStateHandlerImpl::resetToInitialState() {
   AutoMutex am(mutex_);
   SPLAPPTRC(L_DEBUG, "resetToInitialState", "python");
   SplpyGIL lock;
   PyObject * initialCallable = call(loads, pickledInitialCallable);
   if (!initialCallable) {
     SplpyGeneral::tracePythonError();
     throw SplpyGeneral::pythonException("dill.loads");
   }
   Py_DECREF(op->callable());
   op->setCallable(initialCallable);
 }

 Mutex* SplpyOpStateHandlerImpl::getMutex() {
   return &mutex_;
 }

 // Call a python callable with a single argument
 // Caller must hold GILState.
 PyObject * SplpyOpStateHandlerImpl::call(PyObject * callable, PyObject * arg) {
   PyObject * args = PyTuple_New(1);
   Py_INCREF(arg);
   PyTuple_SET_ITEM(args, 0, arg);
   PyObject * ret = PyObject_CallObject(callable, args);
   Py_DECREF(args);
   return ret;
 }

}}

#endif


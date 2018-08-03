#ifndef SPL_SPLPY_CR_H_
#define SPL_SPLPY_CR_H_

#include <SPL/Runtime/Operator/State/ConsistentRegionContext.h>
#include <SPL/Runtime/Operator/State/StateHandler.h>
#include <SPL/Runtime/Utility/Mutex.h>

#include "splpy_general.h"

// Support for consistent region and checkpointing in python operators

namespace streamsx {
  namespace topology {

/** 
 * OptionalConsistentRegionContext<true> holds a pointer to a 
 * ConsistentRegionContext,
 * and ConsistentRegionContext::Permit is an RAII helper
 * for acquiring and releasing a ConsistentRegionPermit.
 * OptionalConsistentRgionContxt<false> does nothing.
 */
template<bool b>
class OptionalConsistentRegionContextImpl;

/**
 * OptionalAutoLockImpl<bool> is intended to be used with an instance of
 * DelegatingStateHandler.  OptionalAutoLockImpl<false> does nothing,
 * while OptionalAutoLockImpl<true> provides an RAII helper for locking
 * and unlocking, using the same mutex as a  DelegatingStateHandler  instance.
 * This can be used to guard the regions of code that modify the state of the 
 * operator that is saved and restored during checkpointing.  
 */
template<bool b>
class OptionalAutoLockImpl;

/**
 * DelegatingStateHandler implements the StateHandler interface in support
 * of checkpointing and consistent region.  The checkpointing and resetting
 * is delegated to the SplpyOp instance owned by the operator extending
 * this class.  For most operators, it is not necessary to override
 * checkpoint, reset, and resetToInitialState, but if operators have state
 * (such as a Window) that needs to be checkpointed and reset, they can
 * override the methods, but they should be sure to call the base method.
 */
class DelegatingStateHandler : public SPL::StateHandler {
public:
  DelegatingStateHandler():locked_(false) {}

  virtual void checkpoint(SPL::Checkpoint & ckpt);
  virtual void reset(SPL::Checkpoint & ckpt);
  virtual void resetToInitialState();

  friend class OptionalAutoLockImpl<true>;

protected:
  virtual SplpyOp * op() = 0;

private:
  void lock();
  void unlock();

  SPL::Mutex mutex_;
  bool locked_;
};

template<>
class OptionalConsistentRegionContextImpl<false> {
 public:
  OptionalConsistentRegionContextImpl(SPL::Operator * op) {}
  class Permit {
  public:
    Permit(OptionalConsistentRegionContextImpl<false>){}
  };
};

template<>
class OptionalConsistentRegionContextImpl<true> {
 public:
  OptionalConsistentRegionContextImpl(SPL::Operator *op) : crContext(NULL) {
    crContext = static_cast<SPL::ConsistentRegionContext *>(op->getContext().getOptionalContext(CONSISTENT_REGION));
  }
  operator SPL::ConsistentRegionContext *() {return crContext;}
  typedef SPL::ConsistentRegionPermit Permit;

private:
  SPL::ConsistentRegionContext * crContext;
};

template<>
class OptionalAutoLockImpl<true> {
public:
  OptionalAutoLockImpl(DelegatingStateHandler* outer) : outer_(outer) {
    outer->lock();
  }
  ~OptionalAutoLockImpl() {
    outer_->unlock();
  }
private:
  DelegatingStateHandler * outer_;
};

template<>
class OptionalAutoLockImpl<false>
{
 public:
  OptionalAutoLockImpl(DelegatingStateHandler*) {}
};

inline void DelegatingStateHandler::checkpoint(SPL::Checkpoint & ckpt) {
  SPLAPPTRC(L_TRACE, "checkpoint: enter", "python");
  OptionalAutoLockImpl<true> lock(this);
  op()->checkpoint(ckpt);
  SPLAPPTRC(L_TRACE, "checkpoint: exit", "python");
}

inline void DelegatingStateHandler::reset(SPL::Checkpoint & ckpt) {
  SPLAPPTRC(L_TRACE, "reset: enter", "python");
  OptionalAutoLockImpl<true> lock(this);
  op()->reset(ckpt);
  SPLAPPTRC(L_TRACE, "reset: exit", "python");
}

inline void DelegatingStateHandler::resetToInitialState() {
  SPLAPPTRC(L_TRACE, "resetToInitialState: enter", "python");
  OptionalAutoLockImpl<true> lock(this);
  op()->resetToInitialState();
  SPLAPPTRC(L_TRACE, "resetToInitialState: exit", "python");
}

inline void DelegatingStateHandler::lock() {
  if (!locked_) {
    mutex_.lock();
    locked_ = true;
  }
}

inline void DelegatingStateHandler::unlock() {
  if (locked_) {
    mutex_.unlock();
    locked_ = false;
  }
}


}} // end namspace

namespace SPL {

  // Hackery 
  template<typename T> struct WTDereferencer;
  template<>
    struct WTDereferencer<PyObject *>
    {
      typedef PyObject * deref_type;
      static PyObject * deref(PyObject *t){ return t; }
      static PyObject const * deref(PyObject const * t) { return t; }
    };

  template <typename T> struct WTReferencer;
  template<>
    struct WTReferencer <PyObject *>
    {
      typedef PyObject * ref_type;
      static PyObject * ref(PyObject * t) { return t; }
      static PyObject const * ref(PyObject const * t) { return t; }
    };

  template<typename T> struct Allocator;
  template<>
    struct Allocator<PyObject *>
    {
      // allocate: seems to be no good way.   Seems not to be needed anyway.
      // Caller must already hold GIL.
      static void deallocate(PyObject * t) { Py_XDECREF(t); }
    };

  template<typename T> struct Referencer;
  template<>
    struct Referencer<PyObject *>
    {      
        typedef PyObject * dereference_t;
        typedef PyObject * reference_t;
        static PyObject * dereference(PyObject * t) { return t; }
        static PyObject const * dereference(PyObject const * t) { return t; }
        static PyObject * reference(PyObject * t) { return t; }
        static PyObject const * reference(PyObject const * t) { return t; }
    };

#if 0  
  PyObject * load(SPL::ByteBuffer<SPL::Checkpoint> & ckpt) {
      using namespace streamsx::topology;
      SPLAPPTRC(L_DEBUG, "load(SPL::Checkpoint &): enter", "python");
      SPL::blob bytes;
      ckpt >> bytes;
      
      SplpyGIL gil;
      
      // TODO ideally we don't load loads every time.
      PyObject * loads = SplpyGeneral::loadFunction("dill", "loads");
      PyObject * args = PyTuple_New(1);
      PyObject * pickle = pySplValueToPyObject(bytes); // TODO probably need to decref pickle
      Py_INCREF(pickle);
      PyTuple_SET_ITEM(args, 0, pickle);
      PyObject * ret = PyObject_CallObject(loads, args);
      Py_DECREF(loads);
      Py_DECREF(args);
      if (!ret) {
        SplpyGeneral::tracePythonError();
        throw SplpyGeneral::pythonException("dill.loads");
      }
      SPLAPPTRC(L_DEBUG, "load(SPL::Checkpoint &): exit", "python");
      
      return ret;
  }

  void dump(SPL::ByteBuffer<SPL::Checkpoint> & ckpt, PyObject * object) {
      using namespace streamsx::topology;

      SPLAPPTRC(L_DEBUG, "dump(SPL::Checkpoint&, PyObject*): enter", "python");
      
      SPL::blob bytes;
      {
        SplpyGIL gil;
        
        // TODO ideally we don't load dumps every time.
        PyObject * dumps = SplpyGeneral::loadFunction("dill", "dumps");
        PyObject * args = PyTuple_New(1);
        Py_INCREF(object);
        PyTuple_SET_ITEM(args, 0, object);
        PyObject * ret = PyObject_CallObject(dumps, args);     
        Py_DECREF(dumps);
        Py_DECREF(args);
        if (ret == NULL) {
          SplpyGeneral::tracePythonError();
          throw SplpyGeneral::pythonException("dill.dumps");
        }
        // ret is the dilled object
        pySplValueFromPyObject(bytes, ret);
        Py_DECREF(ret);
      }
      ckpt << bytes;
      
      SPLAPPTRC(L_DEBUG, "dump(SPL::Checkpoint&, PyObject*): exit", "python");
  }

  

template <class T>
  inline SPL::ByteBuffer<Checkpoint> & operator << (SPL::ByteBuffer<Checkpoint> & ckpt, const std::deque<T> & value);

template<>
  inline SPL::ByteBuffer<SPL::Checkpoint> & operator >> <PyObject*> (SPL::ByteBuffer<SPL::Checkpoint> & ckpt, std::deque<PyObject *> & value) {
    uint32_t size = ckpt.getUInt32();
    if (!value.empty()) {
      SPLAPPTRC(L_ERROR, "Cannot safely clear deque<PyObject*>", "python");
    }
    //value.clear(); // Uh-Oh.  If not already clear, potential problem.
    PyObject * element;
    for (uint32_t i  = 0; i < size; ++i) {
      element = load(ckpt);
      value.push_back(element); // steals reference
    }
    return ckpt;
  }

template <class T>
  inline SPL::ByteBuffer<Checkpoint> & operator << (SPL::ByteBuffer<Checkpoint> & ckpt, const std::deque<T> & value);

template<>
  inline SPL::ByteBuffer<SPL::Checkpoint> & operator << <PyObject*> (SPL::ByteBuffer<SPL::Checkpoint> & ckpt, const std::deque<PyObject *> & value) {
    ckpt.addUInt32(static_cast<uint32_t>(value.size()));
    for(std::deque<PyObject *>::const_iterator it = value.begin();
        it != value.end(); ++it) {
      dump(ckpt, *it);
    }
    return ckpt;
  }
#endif

    Checkpoint & operator <<(Checkpoint & ckpt, PyObject const & obj){
      using namespace streamsx::topology;

      SPLAPPTRC(L_DEBUG, "operator << (Checkpoint&, PyObject const &): enter", "python");
      
      SPL::blob bytes;
      {
        SplpyGIL gil;
        
        // TODO ideally we don't load dumps every time.
        PyObject * dumps = SplpyGeneral::loadFunction("dill", "dumps");
        PyObject * args = PyTuple_New(1);
        PyObject * arg = const_cast<PyObject *>(&obj);
        Py_INCREF(arg);
        PyTuple_SET_ITEM(args, 0, arg);
        PyObject * ret = PyObject_CallObject(dumps, args);     
        Py_DECREF(dumps);
        Py_DECREF(args);
        if (ret == NULL) {
          SplpyGeneral::tracePythonError();
          throw SplpyGeneral::pythonException("dill.dumps");
        }
        // ret is the dilled object
        pySplValueFromPyObject(bytes, ret);
        Py_DECREF(ret);
      }
      ckpt << bytes;
      
      SPLAPPTRC(L_DEBUG, "operator << (Checkpoint&, PyObject const &): exit", "python");
      return ckpt;
    }

    Checkpoint & operator >>(Checkpoint & ckpt, PyObject * & obj){
      using namespace streamsx::topology;
      SPLAPPTRC(L_DEBUG, "operator >> (Checkpoint&, PyObject &): enter", "python");
      SPL::blob bytes;
      ckpt >> bytes;
      
      SplpyGIL gil;
      
      // TODO ideally we don't load loads every time.
      PyObject * loads = SplpyGeneral::loadFunction("dill", "loads");
      PyObject * args = PyTuple_New(1);
      PyObject * pickle = pySplValueToPyObject(bytes); // TODO probably need to decref pickle
      Py_INCREF(pickle);
      PyTuple_SET_ITEM(args, 0, pickle);
      PyObject * ret = PyObject_CallObject(loads, args);
      Py_DECREF(loads);
      Py_DECREF(args);
      if (!ret) {
        SplpyGeneral::tracePythonError();
        throw SplpyGeneral::pythonException("dill.loads");
      }
      // obj is likely uninitialized garbage.  Ignore it, and copy
      // ret into its memory.  It also steals the reference.
      //memcpy(&obj, ret, sizeof(obj));
      obj = ret;
      
      SPLAPPTRC(L_DEBUG, "operator >> (Checkpoint&, PyObject &): exit", "python");
      
      return ckpt;
    }

    ByteBuffer<Checkpoint> & operator<<(ByteBuffer<Checkpoint> & ckpt, PyObject * obj){
      using namespace streamsx::topology;

      SPLAPPTRC(L_DEBUG, "operator << (ByteBuffer<Checkpoint> &, PyObject *): enter", "python");
      
      SPL::blob bytes;
      {
        SplpyGIL gil;
        
        // TODO ideally we don't load dumps every time.
        PyObject * dumps = SplpyGeneral::loadFunction("dill", "dumps");
        PyObject * args = PyTuple_New(1);
        PyObject * arg = obj;
        Py_INCREF(arg);
        PyTuple_SET_ITEM(args, 0, arg);
        PyObject * ret = PyObject_CallObject(dumps, args);     
        Py_DECREF(dumps);
        Py_DECREF(args);
        if (ret == NULL) {
          SplpyGeneral::tracePythonError();
          throw SplpyGeneral::pythonException("dill.dumps");
        }
        // ret is the dilled object
        pySplValueFromPyObject(bytes, ret);
        Py_DECREF(ret);
      }
      ckpt << bytes;
      
      SPLAPPTRC(L_DEBUG, "operator << (ByteBuffer<Checkpoint>&, PyObject *): exit", "python");
      return ckpt;
    }

    ByteBuffer<Checkpoint> & operator>>(ByteBuffer<Checkpoint> & ckpt, PyObject * &obj){
      using namespace streamsx::topology;
      SPLAPPTRC(L_DEBUG, "operator >> (ByteBuffer<Checkpoint>&, PyObject *): enter", "python");
      SPL::blob bytes;
      ckpt >> bytes;
      
      SplpyGIL gil;
      
      // TODO ideally we don't load loads every time.
      PyObject * loads = SplpyGeneral::loadFunction("dill", "loads");
      PyObject * args = PyTuple_New(1);
      PyObject * pickle = pySplValueToPyObject(bytes); // TODO probably need to decref pickle
      Py_INCREF(pickle);
      PyTuple_SET_ITEM(args, 0, pickle);
      PyObject * ret = PyObject_CallObject(loads, args);
      Py_DECREF(loads);
      Py_DECREF(args);
      if (!ret) {
        SplpyGeneral::tracePythonError();
        throw SplpyGeneral::pythonException("dill.loads");
      }
      // obj is likely uninitialized garbage.  Ignore it, and copy
      // ret into its memory.  It also steals the reference.
      //memcpy(obj, ret, sizeof(*obj));
      obj = ret;
      
      SPLAPPTRC(L_DEBUG, "operator >> (ByteBuffer<Checkpoint>&, PyObject *): exit", "python");
      
      return ckpt;
    }
}

// Reference to obj is borrowed.
std::ostream & operator <<(std::ostream &ostr, PyObject const & obj){
    streamsx::topology::SplpyGIL gil;
    // python api is not const correct
    PyObject* str = PyObject_Str(const_cast<PyObject*>(&obj));
    SPL::rstring s;
    if (0 == streamsx::topology::pyRStringFromPyObject(s, str)) {
      ostr << s;
    }
    Py_DECREF(str);
    return ostr;
}

#if 0
std::ostream & operator >>(std::ostream &ostr, const PyObject  & obj){
    return ostr;
}
#endif

#endif // SPL_SPLPY_CR_H_

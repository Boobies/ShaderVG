/*
 * Internal threading primitives for ShaderVG.
 */

#include "shThread.h"

#if defined(_WIN32)

void shMutexInit(SHMutex *mutex)
{
  InitializeCriticalSection(&mutex->cs);
}

void shMutexDestroy(SHMutex *mutex)
{
  DeleteCriticalSection(&mutex->cs);
}

void shMutexLock(SHMutex *mutex)
{
  EnterCriticalSection(&mutex->cs);
}

void shMutexUnlock(SHMutex *mutex)
{
  LeaveCriticalSection(&mutex->cs);
}

void shRecursiveMutexInit(SHRecursiveMutex *mutex)
{
  InitializeCriticalSection(&mutex->cs);
}

void shRecursiveMutexDestroy(SHRecursiveMutex *mutex)
{
  DeleteCriticalSection(&mutex->cs);
}

void shRecursiveMutexLock(SHRecursiveMutex *mutex)
{
  EnterCriticalSection(&mutex->cs);
}

void shRecursiveMutexUnlock(SHRecursiveMutex *mutex)
{
  LeaveCriticalSection(&mutex->cs);
}

static BOOL CALLBACK shOnceCallback(PINIT_ONCE once,
                                    PVOID parameter,
                                    PVOID *context)
{
  void (*function)(void) = (void (*)(void))parameter;
  (void)once;
  (void)context;
  function();
  return TRUE;
}

void shOnce(SHOnce *once, void (*function)(void))
{
  InitOnceExecuteOnce(once, shOnceCallback, function, NULL);
}

SHThreadId shThreadCurrentId(void)
{
  SHThreadId id;
  id.value = GetCurrentThreadId();
  id.valid = 1;
  return id;
}

SHThreadId shThreadInvalidId(void)
{
  SHThreadId id;
  id.value = 0;
  id.valid = 0;
  return id;
}

int shThreadIdEqual(SHThreadId a, SHThreadId b)
{
  return a.valid && b.valid && a.value == b.value;
}

void shAtomicIntInit(SHAtomicInt *value, int initial)
{
  value->value = (LONG)initial;
}

void shAtomicIntDestroy(SHAtomicInt *value)
{
  (void)value;
}

int shAtomicIntLoad(SHAtomicInt *value)
{
  return (int)InterlockedCompareExchange(&value->value, 0, 0);
}

int shAtomicIntIncrement(SHAtomicInt *value)
{
  return (int)InterlockedIncrement(&value->value);
}

int shAtomicIntDecrement(SHAtomicInt *value)
{
  return (int)InterlockedDecrement(&value->value);
}

int shAtomicIntDecrementIfPositive(SHAtomicInt *value)
{
  LONG oldValue;
  LONG newValue;

  do {
    oldValue = InterlockedCompareExchange(&value->value, 0, 0);
    if (oldValue <= 0)
      return (int)oldValue;
    newValue = oldValue - 1;
  } while (InterlockedCompareExchange(&value->value,
                                      newValue,
                                      oldValue) != oldValue);

  return (int)newValue;
}

#else

void shMutexInit(SHMutex *mutex)
{
  pthread_mutex_init(&mutex->mutex, NULL);
}

void shMutexDestroy(SHMutex *mutex)
{
  pthread_mutex_destroy(&mutex->mutex);
}

void shMutexLock(SHMutex *mutex)
{
  pthread_mutex_lock(&mutex->mutex);
}

void shMutexUnlock(SHMutex *mutex)
{
  pthread_mutex_unlock(&mutex->mutex);
}

void shRecursiveMutexInit(SHRecursiveMutex *mutex)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&mutex->mutex, &attr);
  pthread_mutexattr_destroy(&attr);
}

void shRecursiveMutexDestroy(SHRecursiveMutex *mutex)
{
  pthread_mutex_destroy(&mutex->mutex);
}

void shRecursiveMutexLock(SHRecursiveMutex *mutex)
{
  pthread_mutex_lock(&mutex->mutex);
}

void shRecursiveMutexUnlock(SHRecursiveMutex *mutex)
{
  pthread_mutex_unlock(&mutex->mutex);
}

void shOnce(SHOnce *once, void (*function)(void))
{
  pthread_once(once, function);
}

SHThreadId shThreadCurrentId(void)
{
  SHThreadId id;
  id.value = pthread_self();
  id.valid = 1;
  return id;
}

SHThreadId shThreadInvalidId(void)
{
  SHThreadId id;
  id.valid = 0;
  return id;
}

int shThreadIdEqual(SHThreadId a, SHThreadId b)
{
  return a.valid && b.valid && pthread_equal(a.value, b.value);
}

void shAtomicIntInit(SHAtomicInt *value, int initial)
{
  shMutexInit(&value->mutex);
  value->value = initial;
}

void shAtomicIntDestroy(SHAtomicInt *value)
{
  shMutexDestroy(&value->mutex);
}

int shAtomicIntLoad(SHAtomicInt *value)
{
  int result;

  shMutexLock(&value->mutex);
  result = value->value;
  shMutexUnlock(&value->mutex);

  return result;
}

int shAtomicIntIncrement(SHAtomicInt *value)
{
  int result;

  shMutexLock(&value->mutex);
  result = ++value->value;
  shMutexUnlock(&value->mutex);

  return result;
}

int shAtomicIntDecrement(SHAtomicInt *value)
{
  int result;

  shMutexLock(&value->mutex);
  result = --value->value;
  shMutexUnlock(&value->mutex);

  return result;
}

int shAtomicIntDecrementIfPositive(SHAtomicInt *value)
{
  int result;

  shMutexLock(&value->mutex);
  if (value->value > 0)
    --value->value;
  result = value->value;
  shMutexUnlock(&value->mutex);

  return result;
}

#endif

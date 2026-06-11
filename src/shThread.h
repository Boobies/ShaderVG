/*
 * Internal threading primitives for ShaderVG.
 */

#ifndef __SHTHREAD_H
#define __SHTHREAD_H

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SH_CLEANUP(func) __attribute__((cleanup(func)))
#  define SH_HAS_CLEANUP 1
#else
#  define SH_CLEANUP(func)
#  define SH_HAS_CLEANUP 0
#endif

#if defined(_WIN32)
typedef struct
{
  CRITICAL_SECTION cs;
} SHMutex;

typedef struct
{
  CRITICAL_SECTION cs;
} SHRecursiveMutex;

typedef INIT_ONCE SHOnce;
#define SH_ONCE_INIT INIT_ONCE_STATIC_INIT

typedef struct
{
  DWORD value;
  int valid;
} SHThreadId;
#else
typedef struct
{
  pthread_mutex_t mutex;
} SHMutex;

typedef struct
{
  pthread_mutex_t mutex;
} SHRecursiveMutex;

typedef pthread_once_t SHOnce;
#define SH_ONCE_INIT PTHREAD_ONCE_INIT

typedef struct
{
  pthread_t value;
  int valid;
} SHThreadId;
#endif

void shMutexInit(SHMutex *mutex);
void shMutexDestroy(SHMutex *mutex);
void shMutexLock(SHMutex *mutex);
void shMutexUnlock(SHMutex *mutex);

void shRecursiveMutexInit(SHRecursiveMutex *mutex);
void shRecursiveMutexDestroy(SHRecursiveMutex *mutex);
void shRecursiveMutexLock(SHRecursiveMutex *mutex);
void shRecursiveMutexUnlock(SHRecursiveMutex *mutex);

void shOnce(SHOnce *once, void (*function)(void));

SHThreadId shThreadCurrentId(void);
SHThreadId shThreadInvalidId(void);
int shThreadIdEqual(SHThreadId a, SHThreadId b);

#endif /* __SHTHREAD_H */

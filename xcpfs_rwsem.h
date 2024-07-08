#ifndef XCPFS_RWSEM_H
#define XCPFS_RWSEM_H

#include<linux/rwsem.h>

struct xcpfs_rwsem {
    struct rw_semaphore internal_rwsem;
}

#define init_xpcfs_rwsem(sem) \
do{\
    static struct lock_class_key __key;\
    __init_xcpfs_rwsem((sem),#sem,&__key);\
} while(0)

static inline void __init_xcpfs_rwsem(struct xcpfs_rwsem *sem,
		const char *sem_name, struct lock_class_key *key)
{
	__init_rwsem(&sem->internal_rwsem, sem_name, key);
}

static inline int xcpfs_rwsem_is_locked(struct xcpfs_rwsem *sem)
{
	return rwsem_is_locked(&sem->internal_rwsem);
}

static inline int xcpfs_rwsem_is_contended(struct xcpfs_rwsem *sem)
{
	return rwsem_is_contended(&sem->internal_rwsem);
}

static inline void xcpfs_down_read(struct xcpfs_rwsem *sem)
{
	down_read(&sem->internal_rwsem);
}

static inline int xcpfs_down_read_trylock(struct xcpfs_rwsem *sem)
{
	return down_read_trylock(&sem->internal_rwsem);
}

static inline void xcpfs_up_read(struct xcpfs_rwsem *sem)
{
	up_read(&sem->internal_rwsem);
}

static inline void xcpfs_down_write(struct xcpfs_rwsem *sem)
{
	down_write(&sem->internal_rwsem);
}

static inline int xcpfs_down_write_trylock(struct xcpfs_rwsem *sem)
{
	return down_write_trylock(&sem->internal_rwsem);
}

static inline void xcpfs_up_write(struct xcpfs_rwsem *sem)
{
	up_write(&sem->internal_rwsem);
}
#endif
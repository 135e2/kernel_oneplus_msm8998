// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */
#ifndef _ADAPTIVE_TUNE_H_
#define _ADAPTIVE_TUNE_H_

enum adaptune_states {
    CORE,
    INPUT,
    N_ATS
};

struct adaptune {
    atomic_t state[N_ATS], update;
    void *priv;
};

extern struct adaptune atx;

#define adaptune_read_state(_ats) atomic_read(&atx.state[_ats])
#define adaptune_acquire_update()                       \
({                                                          \
    if (adaptune_read_state(INPUT))                         \
        atomic_cmpxchg(&atx.update, 0, 1);    \
})

void adaptive_schedtune_set(bool state);
void adaptune_update(struct adaptune *at);

#endif /* _ADAPTIVE_TUNE_H_ */
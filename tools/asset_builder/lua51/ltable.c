/*
** Lua tables with LuaJIT 2.1's layout and algorithms (Vitalatro asset builder).
**
** This replaces Lua 5.1's ltable.c with a port of LuaJIT 2.1's lj_tab.c (OpenResty branch,
** v2.1-20251030) so that pairs()/next() visit keys in exactly the order LuaJIT does:
**  - the array part holds keys 0..asize-1 (slot 0 included) and is traversed first;
**  - the hash part has 2^hbits nodes (none: the shared dummy node), Brent's variation with a
**    free pointer that only moves down, the same rehash sizing (bestasize) and reinsertion
**    order, and the same rechaining of colliding keys;
**  - strings hash to their sid, which LuaJIT built with LUAJIT_TEST_FIXED_ORDER sets to the
**    sparse string hash (lstring.c), numbers by their IEEE bits (hashrot), booleans by value.
** Keys of nodes whose value became nil stay in place (LuaJIT never removes them outside a
** rehash), see lgc.c.
**
** Copyright (C) 2005-2025 Mike Pall (LuaJIT), 1994-2008 Lua.org, PUC-Rio. See COPYRIGHT.
*/

#include <math.h>
#include <string.h>
#include <stdint.h>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"


#define LJ_MAX_HBITS	26
#define LJ_MAX_ABITS	28
#define LJ_MAX_ASIZE	((1<<(LJ_MAX_ABITS-1))+1)

/* Hash constants (lj_tab.h). */
#define HASH_ROT1	14
#define HASH_ROT2	5
#define HASH_ROT3	13

#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {{NULL}, LUA_TNIL},  /* value */
  {{{NULL}, LUA_TNIL, NULL}}  /* key */
};

#define hmask(t)	((uint32_t)(sizenode(t) - 1))
#define nextnode(n)	gnext(n)

static uint32_t lj_rol (uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

static uint32_t lj_fls (uint32_t x) {
  uint32_t r = 0;
  while (x >>= 1) r++;
  return r;
}

#define hsize2hbits(s)	((s) ? ((s)==1 ? 1 : 1+lj_fls((uint32_t)((s)-1))) : 0)

uint32_t luaH_hsize2hbits (uint32_t s) {
  return hsize2hbits(s);
}

/* (int32_t)n as x86's cvttsd2si: out of range and NaN give INT32_MIN. */
static int32_t lj_num2int (lua_Number n) {
  if (n >= -2147483648.0 && n < 2147483648.0) return (int32_t)n;
  return INT32_MIN;
}

/* lj_tab.h hashrot, the x86/x64 variant (the reference LuaJIT runs on x64). */
static uint32_t hashrot (uint32_t lo, uint32_t hi) {
  lo ^= hi; hi = lj_rol(hi, HASH_ROT1);
  lo -= hi; hi = lj_rol(hi, HASH_ROT2);
  hi ^= lo; hi -= lj_rol(lo, HASH_ROT3);
  return hi;
}

static Node *hashmask (const Table *t, uint32_t hash) {
  return gnode(t, hash & hmask(t));
}

static Node *hashnum (const Table *t, lua_Number n) {
  uint64_t u;
  memcpy(&u, &n, sizeof(u));
  return hashmask(t, hashrot((uint32_t)u, (uint32_t)(u >> 32) << 1));
}

static Node *hashkey (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TSTRING:
      return hashmask(t, rawtsvalue(key)->tsv.hash);
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TBOOLEAN:
      return hashmask(t, bvalue(key) ? 1u : 0u);
    case LUA_TLIGHTUSERDATA: {
      uint64_t p = (uint64_t)(uintptr_t)pvalue(key);
      return hashmask(t, hashrot((uint32_t)p, (uint32_t)(p >> 32)));
    }
    default: {
      uint64_t p = (uint64_t)(uintptr_t)gcvalue(key);
      return hashmask(t, hashrot((uint32_t)p, (uint32_t)(p >> 32)));
    }
  }
}

static int keyequal (const TValue *a, const TValue *b) {
  return luaO_rawequalObj(a, b);
}


/* -- Table creation and destruction -------------------------------------- */

static void clearhpart (Table *t) {
  uint32_t i, hm = hmask(t);
  for (i = 0; i <= hm; i++) {
    Node *n = gnode(t, i);
    gnext(n) = NULL;
    setnilvalue(gkey(n));
    setnilvalue(gval(n));
  }
}

static void newhpart (lua_State *L, Table *t, uint32_t hbits) {
  uint32_t hsize;
  if (hbits > LJ_MAX_HBITS)
    luaG_runerror(L, "table overflow");
  hsize = 1u << hbits;
  t->node = luaM_newvector(L, hsize, Node);
  t->lsizenode = cast_byte(hbits);
  t->lastfree = gnode(t, hsize);  /* freetop */
  clearhpart(t);
}

static void nohpart (Table *t) {
  t->node = cast(Node *, dummynode);
  t->lsizenode = 0;
  t->lastfree = cast(Node *, dummynode);  /* nothing free: the first new key rehashes */
}

static Table *newtab (lua_State *L, uint32_t asize, uint32_t hbits) {
  Table *t = luaM_new(L, Table);
  luaC_link(L, obj2gco(t), LUA_TTABLE);
  t->metatable = NULL;
  t->flags = cast_byte(~0);
  t->array = NULL;
  t->sizearray = 0;
  nohpart(t);
  if (asize > 0) {
    uint32_t i;
    if (asize > LJ_MAX_ASIZE)
      luaG_runerror(L, "table overflow");
    t->array = luaM_newvector(L, asize, TValue);
    t->sizearray = (int)asize;
    for (i = 0; i < asize; i++) setnilvalue(&t->array[i]);
  }
  if (hbits)
    newhpart(L, t, hbits);
  return t;
}

/* lj_tab_new: asize array slots (0..asize-1), 2^hbits hash nodes (0: none). */
Table *luaH_new (lua_State *L, int asize, int hbits) {
  return newtab(L, (uint32_t)asize, (uint32_t)hbits);
}

/* lj_tab_new_ah: lua_createtable()'s sizes. */
Table *luaH_new_ah (lua_State *L, int a, int h) {
  return newtab(L, (uint32_t)(a > 0 ? a+1 : 0), hsize2hbits((uint32_t)(h > 0 ? h : 0)));
}

/* lj_tab_dup: a copy of a constructor template with the same layout; the template's table
** values are markers for keys whose value is stored afterwards, they become nil. */
Table *luaH_dup (lua_State *L, const Table *kt) {
  uint32_t i, hbits = kt->node == dummynode ? 0 : kt->lsizenode;
  Table *t = newtab(L, (uint32_t)kt->sizearray, hbits);
  t->flags = 0;
  for (i = 0; i < (uint32_t)kt->sizearray; i++)
    setobj2t(L, &t->array[i], &kt->array[i]);
  if (hbits) {
    uint32_t hm = hmask(kt);
    t->lastfree = gnode(t, kt->lastfree - kt->node);
    for (i = 0; i <= hm; i++) {
      const Node *kn = gnode(kt, i);
      Node *n = gnode(t, i);
      Node *next = gnext(kn);
      n->i_val = kn->i_val;
      gkey(n)->value = gkey(kn)->value;
      gkey(n)->tt = gkey(kn)->tt;
      if (ttistable(gval(n))) setnilvalue(gval(n));
      gnext(n) = next == NULL ? NULL : gnode(t, next - kt->node);
    }
  }
  return t;
}

void luaH_free (lua_State *L, Table *t) {
  if (t->node != dummynode)
    luaM_freearray(L, t->node, sizenode(t), Node);
  luaM_freearray(L, t->array, t->sizearray, TValue);
  luaM_free(L, t);
}


/* -- Getters ------------------------------------------------------------- */

static const TValue *getinth (Table *t, int32_t key) {
  lua_Number k = (lua_Number)key;
  Node *n = hashnum(t, k);
  do {
    if (ttisnumber(gkey(n)) && nvalue(gkey(n)) == k)
      return gval(n);
  } while ((n = nextnode(n)));
  return NULL;
}

static const TValue *getint (Table *t, int32_t key) {
  if ((uint32_t)key < (uint32_t)t->sizearray) return &t->array[key];
  return getinth(t, key);
}

const TValue *luaH_getnum (Table *t, int key) {
  const TValue *tv = getint(t, (int32_t)key);
  return tv ? tv : luaO_nilobject;
}

const TValue *luaH_getstr (Table *t, TString *key) {
  Node *n = hashmask(t, key->tsv.hash);
  do {
    if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key)
      return gval(n);
  } while ((n = nextnode(n)));
  return luaO_nilobject;
}

const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TSTRING: return luaH_getstr(t, rawtsvalue(key));
    case LUA_TNUMBER: {
      lua_Number nk = nvalue(key);
      int32_t k = lj_num2int(nk);
      if (nk == (lua_Number)k) return luaH_getnum(t, k);
      /* else generic lookup */
    }
    /* FALLTHROUGH */
    default: {
      Node *n = hashkey(t, key);
      do {
        if (keyequal(key2tval(n), key))
          return gval(n);
      } while ((n = nextnode(n)));
      return luaO_nilobject;
    }
  }
}


/* -- Resizing ------------------------------------------------------------ */

static TValue *setint (lua_State *L, Table *t, int32_t key);

static void tab_resize (lua_State *L, Table *t, uint32_t asize, uint32_t hbits) {
  Node *oldnode = t->node;
  uint32_t oldasize = (uint32_t)t->sizearray;
  uint32_t oldhmask = oldnode == dummynode ? 0 : hmask(t);
  int oldlsize = t->lsizenode;
  if (asize > oldasize) {  /* Array part grows? */
    uint32_t i;
    if (asize > LJ_MAX_ASIZE)
      luaG_runerror(L, "table overflow");
    luaM_reallocvector(L, t->array, oldasize, asize, TValue);
    t->sizearray = (int)asize;
    for (i = oldasize; i < asize; i++)
      setnilvalue(&t->array[i]);
  }
  /* Create new (empty) hash part. */
  if (hbits)
    newhpart(L, t, hbits);
  else
    nohpart(t);
  if (asize < oldasize) {  /* Array part shrinks? */
    uint32_t i;
    t->sizearray = (int)asize;
    for (i = asize; i < oldasize; i++)  /* Reinsert old array values. */
      if (!ttisnil(&t->array[i]))
        setobjt2t(L, setint(L, t, (int32_t)i), &t->array[i]);
    luaM_reallocvector(L, t->array, oldasize, asize, TValue);
  }
  if (oldhmask > 0) {  /* Reinsert pairs from old hash part. */
    uint32_t i;
    for (i = 0; i <= oldhmask; i++) {
      Node *n = &oldnode[i];
      if (!ttisnil(gval(n)))
        setobjt2t(L, luaH_set(L, t, key2tval(n)), gval(n));
    }
    luaM_freearray(L, oldnode, twoto(oldlsize), Node);
  }
}

static uint32_t countint (const TValue *key, uint32_t *bins) {
  if (ttisnumber(key)) {
    lua_Number nk = nvalue(key);
    int32_t k = lj_num2int(nk);
    if ((uint32_t)k < LJ_MAX_ASIZE && nk == (lua_Number)k) {
      bins[(k > 2 ? lj_fls((uint32_t)(k-1)) : 0)]++;
      return 1;
    }
  }
  return 0;
}

static uint32_t countarray (const Table *t, uint32_t *bins) {
  uint32_t na, b, i;
  if (t->sizearray == 0) return 0;
  for (na = i = b = 0; b < LJ_MAX_ABITS; b++) {
    uint32_t n, top = 2u << b;
    if (top >= (uint32_t)t->sizearray) {
      top = (uint32_t)t->sizearray-1;
      if (i > top)
        break;
    }
    for (n = 0; i <= top; i++)
      if (!ttisnil(&t->array[i]))
        n++;
    bins[b] += n;
    na += n;
  }
  return na;
}

static uint32_t counthash (const Table *t, uint32_t *bins, uint32_t *narray) {
  uint32_t total, na, i, hm = t->node == dummynode ? 0 : hmask(t);
  for (total = na = 0, i = 0; i <= hm; i++) {
    Node *n = gnode(t, i);
    if (!ttisnil(gval(n))) {
      na += countint(key2tval(n), bins);
      total++;
    }
  }
  *narray += na;
  return total;
}

static uint32_t bestasize (uint32_t bins[], uint32_t *narray) {
  uint32_t b, sum, na = 0, sz = 0, nn = *narray;
  for (b = 0, sum = 0; 2*nn > (1u<<b) && sum != nn; b++)
    if (bins[b] > 0 && 2*(sum += bins[b]) > (1u<<b)) {
      sz = (2u<<b)+1;
      na = sum;
    }
  *narray = sz;
  return na;
}

static void rehashtab (lua_State *L, Table *t, const TValue *ek) {
  uint32_t bins[LJ_MAX_ABITS];
  uint32_t total, asize, na, i;
  for (i = 0; i < LJ_MAX_ABITS; i++) bins[i] = 0;
  asize = countarray(t, bins);
  total = 1 + asize;
  total += counthash(t, bins, &asize);
  asize += countint(ek, bins);
  na = bestasize(bins, &asize);
  total -= na;
  tab_resize(L, t, asize, hsize2hbits(total));
}

/* lj_tab_reasize: array slots 0..nasize, same hash size. */
void luaH_reasize (lua_State *L, Table *t, int nasize) {
  tab_resize(L, t, (uint32_t)nasize+1, t->node == dummynode ? 0 : t->lsizenode);
}


/* -- Setters ------------------------------------------------------------- */

/* Insert new key. Use Brent's variation to optimize the chain length. */
static TValue *newkey (lua_State *L, Table *t, const TValue *key) {
  Node *n = hashkey(t, key);
  if (!ttisnil(gval(n)) || t->node == dummynode) {
    Node *nodebase = t->node;
    Node *collide, *freenode = t->lastfree;
    do {
      if (freenode == nodebase) {  /* No free node found? */
        rehashtab(L, t, key);  /* Rehash table. */
        return luaH_set(L, t, key);  /* Retry key insertion. */
      }
    } while (!ttisnil(gkey(--freenode)));
    t->lastfree = freenode;
    collide = hashkey(t, key2tval(n));
    if (collide != n) {  /* Colliding node not the main node? */
      while (nextnode(collide) != n)  /* Find predecessor. */
        collide = nextnode(collide);
      gnext(collide) = freenode;  /* Relink chain. */
      /* Copy colliding node into free node and free main node. */
      freenode->i_val = n->i_val;
      freenode->i_key = n->i_key;
      gnext(n) = NULL;
      setnilvalue(gval(n));
      /* Rechain pseudo-resurrected string keys with colliding hashes. */
      while (nextnode(freenode)) {
        Node *nn = nextnode(freenode);
        if (!ttisnil(gval(nn)) && hashkey(t, key2tval(nn)) == n) {
          gnext(freenode) = gnext(nn);
          gnext(nn) = gnext(n);
          gnext(n) = nn;
          while ((nn = nextnode(freenode))) {
            if (!ttisnil(gval(nn))) {
              Node *mn = hashkey(t, key2tval(nn));
              if (mn != freenode && mn != nn) {
                gnext(freenode) = gnext(nn);
                gnext(nn) = gnext(mn);
                gnext(mn) = nn;
              } else {
                freenode = nn;
              }
            } else {
              freenode = nn;
            }
          }
          break;
        } else {
          freenode = nn;
        }
      }
    } else {  /* Otherwise use free node. */
      gnext(freenode) = gnext(n);  /* Insert into chain. */
      gnext(n) = freenode;
      n = freenode;
    }
  }
  gkey(n)->value = key->value;
  gkey(n)->tt = key->tt;
  if (ttisnumber(key) && nvalue(key) == 0)
    setnvalue(key2tval(n), 0);  /* -0 is stored as 0 */
  luaC_barriert(L, t, key);
  return gval(n);
}

static TValue *setinth (lua_State *L, Table *t, int32_t key) {
  TValue k;
  Node *n;
  setnvalue(&k, (lua_Number)key);
  n = hashnum(t, nvalue(&k));
  do {
    if (ttisnumber(gkey(n)) && nvalue(gkey(n)) == nvalue(&k))
      return gval(n);
  } while ((n = nextnode(n)));
  return newkey(L, t, &k);
}

static TValue *setint (lua_State *L, Table *t, int32_t key) {
  if ((uint32_t)key < (uint32_t)t->sizearray) return &t->array[key];
  return setinth(L, t, key);
}

TValue *luaH_setnum (lua_State *L, Table *t, int key) {
  return setint(L, t, (int32_t)key);
}

TValue *luaH_setstr (lua_State *L, Table *t, TString *key) {
  TValue k;
  Node *n = hashmask(t, key->tsv.hash);
  do {
    if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key)
      return gval(n);
  } while ((n = nextnode(n)));
  setsvalue(L, &k, key);
  return newkey(L, t, &k);
}

TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  Node *n;
  t->flags = 0;
  switch (ttype(key)) {
    case LUA_TSTRING: return luaH_setstr(L, t, rawtsvalue(key));
    case LUA_TNIL: luaG_runerror(L, "table index is nil"); break;
    case LUA_TNUMBER: {
      lua_Number nk = nvalue(key);
      int32_t k = lj_num2int(nk);
      if (nk == (lua_Number)k)
        return setint(L, t, k);
      if (luai_numisnan(nk))
        luaG_runerror(L, "table index is NaN");
      break;
    }
    default: break;
  }
  n = hashkey(t, key);
  do {
    if (keyequal(key2tval(n), key))
      return gval(n);
  } while ((n = nextnode(n)));
  return newkey(L, t, key);
}


/* -- Traversal ----------------------------------------------------------- */

/* Successor traversal index of a key: array 0..asize-1, hash asize..asize+hmask. */
static uint32_t keyindex (lua_State *L, Table *t, const TValue *key) {
  if (ttisnumber(key)) {
    lua_Number nk = nvalue(key);
    int32_t k = lj_num2int(nk);
    if ((uint32_t)k < (uint32_t)t->sizearray && nk == (lua_Number)k)
      return (uint32_t)k + 1;
  }
  if (!ttisnil(key)) {
    Node *n = hashkey(t, key);
    do {
      if (keyequal(key2tval(n), key))
        return (uint32_t)t->sizearray + (uint32_t)((n+1) - t->node);
    } while ((n = nextnode(n)));
    luaG_runerror(L, "invalid key to " LUA_QL("next"));
  }
  return 0;  /* A nil key starts the traversal. */
}

int luaH_next (lua_State *L, Table *t, StkId key) {
  uint32_t idx = keyindex(L, t, key);
  uint32_t hm = t->node == dummynode ? 0 : hmask(t);
  for (; idx < (uint32_t)t->sizearray; idx++) {
    if (!ttisnil(&t->array[idx])) {
      setnvalue(key, cast_num(idx));
      setobj2s(L, key+1, &t->array[idx]);
      return 1;
    }
  }
  idx -= (uint32_t)t->sizearray;
  if (t->node == dummynode) return 0;
  for (; idx <= hm; idx++) {
    Node *n = gnode(t, idx);
    if (!ttisnil(gval(n))) {
      setobj2s(L, key, key2tval(n));
      setobj2s(L, key+1, gval(n));
      return 1;
    }
  }
  return 0;
}


/* -- Length -------------------------------------------------------------- */

static int tab_len_slow (Table *t, size_t hi) {
  const TValue *tv;
  size_t lo = hi;
  hi++;
  while ((tv = getint(t, (int32_t)hi)) && !ttisnil(tv)) {
    lo = hi;
    hi += hi;
    if (hi > (size_t)(0x7fffffff - 2)) {
      lo = 1;
      while ((tv = getint(t, (int32_t)lo)) && !ttisnil(tv)) lo++;
      return (int)(lo - 1);
    }
  }
  while (hi - lo > 1) {
    size_t mid = (lo+hi) >> 1;
    const TValue *tvb = getint(t, (int32_t)mid);
    if (tvb && !ttisnil(tvb)) lo = mid; else hi = mid;
  }
  return (int)lo;
}

/* lj_tab_len: the border LuaJIT's # finds. */
int luaH_getn (Table *t) {
  size_t hi = (size_t)t->sizearray;
  if (hi) hi--;
  if (hi > 0 && ttisnil(&t->array[hi])) {
    size_t lo = 0;
    while (hi - lo > 1) {
      size_t mid = (lo+hi) >> 1;
      if (ttisnil(&t->array[mid])) hi = mid; else lo = mid;
    }
    return (int)lo;
  }
  return t->node != dummynode ? tab_len_slow(t, hi) : (int)hi;
}


#if defined(LUA_DEBUG)
Node *luaH_mainposition (const Table *t, const TValue *key) {
  return hashkey(t, key);
}

int luaH_isdummy (Node *n) { return n == dummynode; }
#endif

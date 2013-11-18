// vim: ts=2:sw=2:noexpandtab

#include "diana.h"

#include <string.h>
#include <limits.h>

#include <stdio.h>

static void *_malloc(struct diana *diana, size_t size);
static void *_realloc(struct diana *diana, void *ptr, size_t oldSize, size_t newSize);
static void _free(struct diana *diana, void *ptr);

/* UNUSED
// ============================================================================
// VECTOR
struct _vector {
	size_t size;
	unsigned int capacity;
	unsigned int length;
	void *data;
};

void _vector_reserve(struct diana *diana, struct _vector *v, unsigned int capacity) {
	v->data = _realloc(diana, v->data, v->size * v->capacity, v->size * capacity);
	v->capacity = capacity;
}

void _vector_init(struct diana *diana, struct _vector *v, size_t size, unsigned int capacity) {
	memset(v, 0, sizeof(*v));
	v->size = size;
	_vector_reserve(diana, v, capacity);
}

void _vector_push(struct diana *diana, struct _vector *v, void *data) {
	unsigned int length = ++v->length;
	if(length > v->capacity) {
		_vector_reserve(diana, v, (length + 1) * 1.5);
	}
	if(data != NULL) {
		memcpy((unsigned char *)v->data + v->length * v->size, data, v->size);
	}
}

int _vector_pop(struct diana *diana, struct _vector *v, void *r) {
	if(v->length == 0) {
		return 0;
	}
	if(r != NULL) {
		memcpy(r, (unsigned char *)v->data + --v->length * v->size, v->size);
	}
	return 1;
}
*/

// ============================================================================
// SPARSE INTEGER SET
// - faster clearing
// - easy tracking of population
// - can be used to 'pop' elements off
// - and iterating over
// - used for delaying entity adding, enabling, disabled and deleting
// - used by each system to track entities it has
struct _sparseIntegerSet {
	unsigned int *dense;
	unsigned int *sparse;
	unsigned int population;
	unsigned int capacity;
};

/* UNUSED
static int _sparseIntegerSet_contains(struct diana *diana, struct _sparseIntegerSet *is, unsigned int i) {
	if(i >= is->capacity) {
		return 0;
	}
	unsigned int a = is->sparse[i];
	unsigned int n = is->population;
	return a < n && is->dense[a] == i;
}
*/

static int _sparseIntegerSet_insert(struct diana *diana, struct _sparseIntegerSet *is, unsigned int i) {
	if(i >= is->capacity) {
		unsigned int newCapacity = (i + 1) * 1.5;
		is->dense = _realloc(diana, is->dense, is->capacity * sizeof(unsigned int), newCapacity * sizeof(unsigned int));
		is->sparse = _realloc(diana, is->sparse, is->capacity * sizeof(unsigned int), newCapacity * sizeof(unsigned int));
		is->capacity = newCapacity;
	}
	unsigned int a = is->sparse[i];
	unsigned int n = is->population;
	if(a >= n || is->dense[a] != i) {
		is->sparse[i] = n;
		is->dense[n] = i;
		is->population = n + 1;
		return 0;
	}
	return 1;
}

static int _sparseIntegerSet_delete(struct diana *diana, struct _sparseIntegerSet *is, unsigned int i) {
	if(i >= is->capacity || is->population == 0) {
		return 0;
	}
	unsigned int a = is->sparse[i];
	unsigned int n = is->population - 1;
	if(a <= n || is->dense[a] == i) {
		unsigned int e = is->dense[n];
		is->population = n;
		is->dense[n] = e;
		is->sparse[e] = a;
		return 1;
	}
	return 0;
}

static void _sparseIntegerSet_clear(struct diana *diana, struct _sparseIntegerSet *is) {
	is->population = 0;
}

static int _sparseIntegerSet_isEmpty(struct diana *diana, struct _sparseIntegerSet *is) {
	return is->population == 0;
}

static unsigned int _sparseIntegerSet_pop(struct diana *diana, struct _sparseIntegerSet *is) {
	if(is->population >= 1) {
		return is->dense[--is->population];
	}
	return UINT_MAX;
}

static void _sparseIntegerSet_free(struct diana *diana, struct _sparseIntegerSet *is) {
	_free(diana, is->dense);
	_free(diana, is->sparse);
	memset(is, 0, sizeof(*is));
}

// ============================================================================
// DENSE INTEGER SET
// - more memory effecient
// - used for tracking components each entity is using
// - and for the active entities
static int _bits_set(unsigned char *bytes, unsigned int bit) {
	int r = !!(bytes[bit >> 3] & (1 << (bit & 7)));
	bytes[bit >> 3] |= (1 << (bit & 7));
	return r;
}

static int _bits_isSet(unsigned char *bytes, unsigned int bit) {
	return !!(bytes[bit >> 3] & (1 << (bit & 7)));
}

static int _bits_clear(unsigned char *bytes, unsigned int bit) {
	int r = !!(bytes[bit >> 3] & (1 << (bit & 7)));
	bytes[bit >> 3] &= ~(1 << (bit & 7));
	return r;
}

// ============================================================================

struct _denseIntegerSet {
	unsigned char *bytes;
	unsigned int capacity;
};

static int _denseIntegerSet_contains(struct diana *diana, struct _denseIntegerSet *is, unsigned int i) {
	return i < is->capacity && _bits_isSet(is->bytes, i);
}

static void _denseIntegerSet_insert(struct diana *diana, struct _denseIntegerSet *is, unsigned int i) {
	if(i >= is->capacity) {
		unsigned int newCapacity = (i + 1) * 1.5;
		is->bytes = _realloc(diana, is->bytes, (is->capacity + 7) >> 3, (newCapacity + 7) >> 3);
		is->capacity = newCapacity;
	}
	_bits_set(is->bytes, i);
}

static void _denseIntegerSet_delete(struct diana *diana, struct _denseIntegerSet *is, unsigned int i) {
	if(i < is->capacity) {
		_bits_clear(is->bytes, i);
	}
}

/* UNUSED
static void _denseIntegerSet_clear(struct diana *diana, struct _denseIntegerSet *is) {
	memset(is->bytes, 0, (is->capacity + 7) >> 3);
}

static int _denseIntegerSet_isEmpty(struct diana *diana, struct _denseIntegerSet *is) {
	unsigned int n = (is->capacity + 7) >> 3, i = 0;
	for(; i < n; i++) {
		if(is->bytes[i]) {
			return 0;
		}
	}
	return 1;
}
*/

static void _denseIntegerSet_free(struct diana *diana, struct _denseIntegerSet *is) {
	_free(diana, is->bytes);
	memset(is, 0, sizeof(*is));
}

// ============================================================================
// PRIMARY DATA
struct _componentBag {
	unsigned int count;
	unsigned int *indexes;
};

struct _component {
	const char *name;
	size_t size;
	size_t offset;
	unsigned int flags;

	void **data;
	struct _sparseIntegerSet freeDataIndexes;
	unsigned int nextDataIndex;
};

static void _component_free(struct diana *diana, struct _component *component) {
	unsigned int i = 0;
	_free(diana, (void *)component->name);
	for(i = 0; i < component->nextDataIndex; i++) {
		_free(diana, component->data[i]);
	}
	_free(diana, component->data);
	_sparseIntegerSet_free(diana, &component->freeDataIndexes);
	memset(component, 0, sizeof(*component));
}

struct _system {
	const char *name;
	unsigned int flags;
	void *userData;
	void (*starting)(struct diana *, void *user_data);
	void (*process)(struct diana *, void *user_data, unsigned int entity, float delta);
	void (*ending)(struct diana *, void *user_data);
	void (*subscribed)(struct diana *, void *user_data, unsigned int entity);
	void (*unsubscribed)(struct diana *, void *user_data, unsigned int entity);
	struct _sparseIntegerSet watch;
	struct _sparseIntegerSet exclude;
	struct _sparseIntegerSet entities;
};

static void _system_free(struct diana *diana, struct _system *system) {
	_free(diana, (void *)system->name);
	_sparseIntegerSet_free(diana, &system->watch);
	_sparseIntegerSet_free(diana, &system->exclude);
	_sparseIntegerSet_free(diana, &system->entities);
	memset(system, 0, sizeof(*system));
}

struct _manager {
	const char *name;
	unsigned int flags;
	void *userData;
	void (*added)(struct diana *diana, void *userData, unsigned int entity);
	void (*enabled)(struct diana *diana, void *userData, unsigned int entity);
	void (*disabled)(struct diana *diana, void *userData, unsigned int entity);
	void (*deleted)(struct diana *diana, void *userData, unsigned int entity);
};

static void _manager_free(struct diana *diana, struct _manager *manager) {
	_free(diana, (void *)manager->name);
	memset(manager, 0, sizeof(*manager));
}

struct diana {
	void *(*malloc)(size_t);
	void (*free)(void *);

	unsigned int error;
	int initialized;
	int processing;

	// manage the entity ids
	// reuse deleted entity ids
	struct _sparseIntegerSet freeEntityIds;
	unsigned int nextEntityId;

	// entity data
	// first 'column' is bits of components defined
	// the rest are the components
	unsigned int dataWidth;
	unsigned int dataHeight;
	unsigned int dataHeightCapacity;
	void *data;

	unsigned int processingDataHeight;
	void **processingData;

	// buffer entity status notifications
	struct _sparseIntegerSet added;
	struct _sparseIntegerSet enabled;
	struct _sparseIntegerSet disabled;
	struct _sparseIntegerSet deleted;

	// all active entities (added and enabled)
	struct _denseIntegerSet active;

	unsigned int num_components;
	struct _component *components;

	unsigned int num_systems;
	struct _system *systems;

	unsigned int num_managers;
	struct _manager *managers;
};

// ============================================================================
// UTILITY
#define FOREACH_SPARSEINTSET(I, N, S) for(N = 0, I = (S)->dense ? (S)->dense[N] : 0; N < (S)->population; N++, I = (S)->dense[N])
#define FOREACH_ARRAY(T, N, A, S) for(N = 0, T = A; N < S; N++, T++)

static void *_malloc(struct diana *diana, size_t size) {
	void *r = diana->malloc(size);
	if(r == NULL) {
		diana->error = DL_ERROR_OUT_OF_MEMORY;
		return NULL;
	}
	memset(r, 0, size);
	return r;
}

static void _free(struct diana *diana, void *ptr) {
	if(ptr != NULL) {
		diana->free(ptr);
	}
}

static char *_strdup(struct diana *diana, const char *s) {
	unsigned int l;
	char *r;
	if(s == NULL) {
		return NULL;
	}
	l = strlen(s);
	r = diana->malloc(l + 1);
	if(r == NULL) {
		diana->error = DL_ERROR_OUT_OF_MEMORY;
		return NULL;
	}
	memcpy(r, s, l + 1);
	return r;
}

static void *_realloc(struct diana *diana, void *ptr, size_t oldSize, size_t newSize) {
	if(oldSize == newSize) {
		return ptr;
	}
	if(newSize == 0) {
		_free(diana, ptr);
		return NULL;
	}
	void *r = diana->malloc(newSize);
	if(r == NULL) {
		diana->error = DL_ERROR_OUT_OF_MEMORY;
		return NULL;
	}
	if(oldSize < newSize) {
		memset((unsigned char *)r + oldSize, 0, newSize - oldSize);
	}
	if(ptr != NULL) {
		memcpy(r, ptr, oldSize < newSize ? oldSize : newSize);
		diana->free(ptr);
	}
	return r;
}

struct diana *allocate_diana(void *(*malloc)(size_t), void (*free)(void *)) {
	struct diana *r = malloc(sizeof(*r));
	if(r != NULL) {
		memset(r, 0, sizeof(*r));
		r->malloc = malloc;
		r->free = free;
	}
	return r;
}

unsigned int diana_getError(struct diana *diana) {
	unsigned int r = diana->error;
	diana->error = DL_ERROR_NONE;
	return r;
}

static void _fixData(struct diana *diana);

void diana_free(struct diana *diana) {
	struct _component *component;
	struct _system *system;
	struct _manager *manager;
	unsigned int i, j;

	_fixData(diana);

	for(i = 0; i < diana->nextEntityId; i++) {
		for(j = 0; j < diana->num_components; j++) {
			diana_removeComponents(diana, i, j);
		}
	}

	_free(diana, diana->data);
	_sparseIntegerSet_free(diana, &diana->freeEntityIds);
	_sparseIntegerSet_free(diana, &diana->added);
	_sparseIntegerSet_free(diana, &diana->enabled);
	_sparseIntegerSet_free(diana, &diana->disabled);
	_sparseIntegerSet_free(diana, &diana->deleted);
	_denseIntegerSet_free(diana, &diana->active);

	FOREACH_ARRAY(component, i, diana->components, diana->num_components) {
		_component_free(diana, component);
	}
	_free(diana, diana->components);

	FOREACH_ARRAY(system, i, diana->systems, diana->num_systems) {
		_system_free(diana, system);
	}
	_free(diana, diana->systems);

	FOREACH_ARRAY(manager, i, diana->managers, diana->num_managers) {
		_manager_free(diana, manager);
	}
	_free(diana, diana->managers);

	diana->free(diana);
}

// ============================================================================
// INITIALIZATION TIME
void diana_initialize(struct diana *diana) {
	unsigned int extraBytes = (diana->num_components + 7) >> 3, n;
	struct _component *c;

	if(diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	FOREACH_ARRAY(c, n, diana->components, diana->num_components) {
		c->offset += extraBytes;
	}

	diana->dataWidth += extraBytes;

	diana->initialized = 1;
}

// ============================================================================
// component
unsigned int diana_createComponent(
	struct diana *diana,
	const char *name,
 	size_t size,
 	unsigned int flags
) {
	struct _component c;

	if(diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return 0;
	}

	memset(&c, 0, sizeof(c));
	c.name = _strdup(diana, name);
	if(c.name == NULL) {
		return 0;
	}
	c.size = size;
	c.flags = flags;
	c.offset = diana->dataWidth;

	if(flags & DL_COMPONENT_MULTIPLE_BIT) {
		diana->dataWidth += sizeof(struct _componentBag);
	} else if(flags & DL_COMPONENT_INDEXED_BIT) {
		diana->dataWidth += sizeof(unsigned int);
	} else {
		diana->dataWidth += size;
	}

	if(flags & DL_COMPONENT_LIMITED_BIT) {
		unsigned int count = (flags >> 3);
		c.data = _malloc(diana, c.size * count);
		while(count) {
			_sparseIntegerSet_insert(diana, &c.freeDataIndexes, count--);
		}
	}

	diana->components = _realloc(diana, diana->components, sizeof(*diana->components) * diana->num_components, sizeof(*diana->components) * (diana->num_components + 1));
	diana->components[diana->num_components++] = c;

	return diana->num_components - 1;
}

// ============================================================================
// system
unsigned int diana_createSystem(
	struct diana *diana,
	const char *name,
	void (*starting)(struct diana *, void *),
	void (*process)(struct diana *, void *, unsigned int, float),
	void (*ending)(struct diana *, void *),
	void (*subscribed)(struct diana *, void *, unsigned int),
	void (*unsubscribed)(struct diana *, void *, unsigned int),
	void *userData,
	unsigned int flags
) {
	struct _system s;

	if(diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return 0;
	}

	memset(&s, 0, sizeof(s));
	s.name = _strdup(diana, name);
	if(s.name == NULL) {
		return 0;
	}
	s.starting = starting;
	s.process = process;
	s.ending = ending;
	s.subscribed = subscribed;
	s.unsubscribed = unsubscribed;
	s.userData = userData;
	s.flags = flags;

	diana->systems = _realloc(diana, diana->systems, sizeof(*diana->systems) * diana->num_systems, sizeof(*diana->systems) * (diana->num_systems + 1));
	diana->systems[diana->num_systems++] = s;

	return diana->num_systems - 1;
}

void diana_watch(struct diana *diana, unsigned int system, unsigned int component) {
	if(diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if(system >= diana->num_systems) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	_sparseIntegerSet_insert(diana, &diana->systems[system].watch, component);
}

void diana_exclude(struct diana *diana, unsigned int system, unsigned int component) {
	if(diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if(system >= diana->num_systems) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	_sparseIntegerSet_insert(diana, &diana->systems[system].exclude, component);
}

// ============================================================================
// manager
unsigned int diana_createManager(
	struct diana *diana,
	const char *name,
	void (*added)(struct diana *, void *, unsigned int),
	void (*enabled)(struct diana *, void *, unsigned int),
	void (*disabled)(struct diana *, void *, unsigned int),
	void (*deleted)(struct diana *, void *, unsigned int),
	void *userData,
	unsigned int flags
) {
	struct _manager m;

	if(diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return 0;
	}

	memset(&m, 0, sizeof(m));
	m.name = _strdup(diana, name);
	if(m.name == NULL) {
		return 0;
	}
	m.added = added;
	m.enabled = enabled;
	m.disabled = disabled;
	m.deleted = deleted;
	m.userData = userData;
	m.flags = flags;

	diana->managers = _realloc(diana, diana->managers, sizeof(*diana->managers) * diana->num_managers, sizeof(*diana->managers) * (diana->num_managers + 1));
	diana->managers[diana->num_managers++] = m;

	return diana->num_managers - 1;
}

// ============================================================================
// RUNTIME
static unsigned char *_getEntityData(struct diana *diana, unsigned int entity) {
	if(entity >= diana->dataHeightCapacity) {
		return diana->processingData[entity - diana->dataHeightCapacity];
	}
	return (void *)((unsigned char *)diana->data + (diana->dataWidth * entity));
}

static void _subscribe(struct diana *diana, struct _system *system, unsigned int entity) {
	int included = _sparseIntegerSet_insert(diana, &system->entities, entity);
	if(!included && system->subscribed != NULL) {
		system->subscribed(diana, system->userData, entity);
	}
}

static void _unsubscribe(struct diana *diana, struct _system *system, unsigned int entity) {
	int included = _sparseIntegerSet_delete(diana, &system->entities, entity);
	if(included && system->unsubscribed != NULL) {
		system->unsubscribed(diana, system->userData, entity);
	}
}

static void _check(struct diana *diana, struct _system *system, unsigned int entity) {
	unsigned char *entity_components = _getEntityData(diana, entity);
	unsigned int component, i;
	int wanted = 1;

	FOREACH_SPARSEINTSET(component, i, &system->watch) {
		if(!(entity_components[component >> 3] & (1 << (component & 7)))) {
			wanted = 0;
			break;
		}
	}

	FOREACH_SPARSEINTSET(component, i, &system->exclude) {
		if((entity_components[component >> 3] & (1 << (component & 7)))) {
			wanted = 0;
			break;
		}
	}

	if(wanted) {
		_subscribe(diana, system, entity);
	} else {
		_unsubscribe(diana, system, entity);
	}
}

static void _fixData(struct diana *diana) {
	// take care of spawns that happen during processing
	if(diana->processingData != NULL) {
		unsigned int newDataHeight = diana->dataHeight + diana->processingDataHeight, i;

		if(newDataHeight >= diana->dataHeightCapacity) {
			unsigned int newDataHeightCapacity = (newDataHeight + 1) * 1.5;
			diana->data = _realloc(diana, diana->data, diana->dataWidth * diana->dataHeightCapacity, diana->dataWidth * newDataHeightCapacity);
			diana->dataHeightCapacity = newDataHeightCapacity;
		}

		for(i = 0; i < diana->processingDataHeight; i++) {
			memcpy((unsigned char *)diana->data + (diana->dataWidth * (diana->dataHeight + i)), diana->processingData[i], diana->dataWidth);
			diana->free(diana->processingData[i]);
		}
		diana->free(diana->processingData);

		diana->dataHeight = newDataHeight;

		diana->processingData = NULL;
		diana->processingDataHeight = 0;
	}
}

void diana_process(struct diana *diana, float delta) {
	unsigned int entity, i, j;
	struct _system *system;
	struct _manager *manager;
	
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	diana->processing = 1;

	FOREACH_SPARSEINTSET(entity, i, &diana->added) {
		/*
		FOREACH_ARRAY(system, j, diana->systems, diana->num_systems) {
			_check(diana, system, entity);
		}
		*/
		FOREACH_ARRAY(manager, j, diana->managers, diana->num_managers) {
			if(manager->added != NULL) {
				manager->added(diana, manager->userData, entity);
			}
		}
	}
	_sparseIntegerSet_clear(diana, &diana->added);

	FOREACH_SPARSEINTSET(entity, i, &diana->enabled) {
		FOREACH_ARRAY(system, j, diana->systems, diana->num_systems) {
			_check(diana, system, entity);
		}
		FOREACH_ARRAY(manager, j, diana->managers, diana->num_managers) {
			if(manager->enabled != NULL) {
				manager->enabled(diana, manager->userData, entity);
			}
		}
		_denseIntegerSet_insert(diana, &diana->active, entity);
	}
	_sparseIntegerSet_clear(diana, &diana->enabled);

	FOREACH_SPARSEINTSET(entity, i, &diana->disabled) {
		FOREACH_ARRAY(system, j, diana->systems, diana->num_systems) {
			_unsubscribe(diana, system, entity);
		}
		FOREACH_ARRAY(manager, j, diana->managers, diana->num_managers) {
			if(manager->disabled != NULL) {
				manager->disabled(diana, manager->userData, entity);
			}
		}
		_denseIntegerSet_delete(diana, &diana->active, entity);
	}
	_sparseIntegerSet_clear(diana, &diana->disabled);

	FOREACH_SPARSEINTSET(entity, i, &diana->deleted) {
		FOREACH_ARRAY(system, j, diana->systems, diana->num_systems) {
			_unsubscribe(diana, system, entity);
		}
		FOREACH_ARRAY(manager, j, diana->managers, diana->num_managers) {
			if(manager->deleted != NULL) {
				manager->deleted(diana, manager->userData, entity);
			}
		}
		for(j = 0; j < diana->num_components; j++) {
			diana_removeComponents(diana, entity, j);
		}
		_sparseIntegerSet_insert(diana, &diana->freeEntityIds, entity);
	}
	_sparseIntegerSet_clear(diana, &diana->deleted);

	FOREACH_ARRAY(system, j, diana->systems, diana->num_systems) {
		if(system->flags & DL_SYSTEM_PASSIVE_BIT) {
			continue;
		}

		if(system->starting != NULL) {
			system->starting(diana, system->userData);
		}
		FOREACH_SPARSEINTSET(entity, i, &system->entities) {
			system->process(diana, system->userData, entity, delta);
		}
		if(system->ending != NULL) {
			system->ending(diana, system->userData);
		}
	}

	diana->processing = 0;

	_fixData(diana);
}

void diana_processSystem(struct diana *diana, unsigned int system, float delta) {
	struct _system *s;
	unsigned int i, entity;

	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if(system >= diana->num_systems) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	s = diana->systems + system;

	if(s->starting != NULL) {
		s->starting(diana, s->userData);
	}
	FOREACH_SPARSEINTSET(entity, i, &s->entities) {
		s->process(diana, s->userData, entity, delta);
	}
	if(s->ending != NULL) {
		s->ending(diana, s->userData);
	}

	_fixData(diana);
}

// ============================================================================
// entity
unsigned int diana_spawn(struct diana *diana) {
	unsigned int r;

	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return 0;
	}

	if(_sparseIntegerSet_isEmpty(diana, &diana->freeEntityIds)) {
		r = diana->nextEntityId++;
	} else {
		r = _sparseIntegerSet_pop(diana, &diana->freeEntityIds);
	}

	diana->dataHeight = diana->dataHeight > (r + 1) ? diana->dataHeight : (r + 1);

	if(diana->dataHeight > diana->dataHeightCapacity) {
		if(diana->processing) {
			void *entityData = _malloc(diana, diana->dataWidth);

			if(entityData == NULL) {
				return 0;
			}

			diana->processingData = _realloc(diana, diana->processingData, sizeof(*diana->processingData) * diana->processingDataHeight, sizeof(*diana->processingData) * (diana->processingDataHeight + 1));

			if(diana->processingData == NULL) {
				return 0;
			}

			diana->processingData[diana->processingDataHeight++] = entityData;
		} else {
			unsigned int newDataHeightCapacity = diana->dataHeight * 1.5;
			diana->data = _realloc(diana, diana->data, diana->dataWidth * diana->dataHeightCapacity, diana->dataWidth * newDataHeightCapacity);
			diana->dataHeightCapacity = newDataHeightCapacity;
		}
	}

	return r;
}

void diana_signal(struct diana *diana, unsigned int entity, unsigned int signal) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	switch(signal) {
	case DL_ENTITY_ADDED:
		_sparseIntegerSet_insert(diana, &diana->added, entity);
		_sparseIntegerSet_insert(diana, &diana->enabled, entity);
		_sparseIntegerSet_delete(diana, &diana->disabled, entity);
		_sparseIntegerSet_delete(diana, &diana->deleted, entity);
		break;
	case DL_ENTITY_ENABLED:
		_sparseIntegerSet_insert(diana, &diana->enabled, entity);
		_sparseIntegerSet_delete(diana, &diana->disabled, entity);
		_sparseIntegerSet_delete(diana, &diana->deleted, entity);
		break;
	case DL_ENTITY_DISABLED:
		_sparseIntegerSet_delete(diana, &diana->enabled, entity);
		_sparseIntegerSet_insert(diana, &diana->disabled, entity);
		_sparseIntegerSet_delete(diana, &diana->deleted, entity);
		break;
	case DL_ENTITY_DELETED:
		_sparseIntegerSet_delete(diana, &diana->added, entity);
		_sparseIntegerSet_delete(diana, &diana->enabled, entity);
		_sparseIntegerSet_insert(diana, &diana->disabled, entity);
		_sparseIntegerSet_insert(diana, &diana->deleted, entity);
		break;
	default:
		diana->error = DL_ERROR_INVALID_VALUE;
	}
}

static unsigned int _getAComponentIndex(struct diana *diana, struct _component *c) {
	unsigned int index;

	if(_sparseIntegerSet_isEmpty(diana, &c->freeDataIndexes)) {
		if(c->flags & DL_COMPONENT_LIMITED_BIT) {
			diana->error = DL_ERROR_FULL_COMPONENT;
			return UINT_MAX;
		}

		index = c->nextDataIndex++;

		c->data = _realloc(diana, c->data, sizeof(void *) * index, sizeof(void *) * c->nextDataIndex);
		c->data[index] = _malloc(diana, c->size);
	} else {
		index = _sparseIntegerSet_pop(diana, &c->freeDataIndexes);
	}

	return index;
}

static void _setComponentI(struct diana *diana, unsigned int entity, unsigned int component, unsigned int i, const void * data) {
	unsigned char *entityData = _getEntityData(diana, entity);
	struct _component *c = diana->components + component;
	int defined = _bits_set(entityData, component);
	void *componentData = NULL;

	// can only add a component if its inactive or if it already has that component
	if(_denseIntegerSet_contains(diana, &diana->active, entity) && !_bits_isSet(entityData, component)) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if(c->flags & DL_COMPONENT_MULTIPLE_BIT) {
		struct _componentBag *bag = (struct _componentBag *)(entityData + c->offset);
		unsigned int index;

		if(i >= bag->count) {
			index = _getAComponentIndex(diana, c);

			if(index == UINT_MAX) {
				return;
			}

			bag->indexes = _realloc(diana, bag->indexes, sizeof(unsigned int) * bag->count, sizeof(unsigned int) * (bag->count + 1));
			bag->indexes[i = bag->count++] = index;
		}

		componentData = (void *)((unsigned char *)c->data[bag->indexes[i]]);
	} else if(c->flags & DL_COMPONENT_INDEXED_BIT) {
		unsigned int *index = (unsigned int *)(entityData + c->offset);

		if(!defined) {
			*index = _getAComponentIndex(diana, c);

			if(*index == UINT_MAX) {
				*index = 0;
				return;
			}
		}

		componentData = (void *)((unsigned char *)c->data[*index]);
	} else {
		componentData = (void *)(entityData + c->offset);
	}

	if(data != NULL) {
		memcpy(componentData, data, c->size);
	}
}

static void * _getComponentI(struct diana *diana, unsigned int entity, unsigned int component, unsigned int i) {
	unsigned char *entityData = _getEntityData(diana, entity);
	struct _component *c = diana->components + component;

	if(!_bits_isSet(entityData, component)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return NULL;
	}

	if(c->flags & DL_COMPONENT_MULTIPLE_BIT) {
		struct _componentBag *bag = (struct _componentBag *)(entityData + c->offset);
		if(i >= bag->count) {
			diana->error = DL_ERROR_INVALID_VALUE;
			return NULL;
		}
		return (void *)((unsigned char *)c->data[bag->indexes[i]]);
	}

	if(c->flags & DL_COMPONENT_INDEXED_BIT) {
		unsigned int *index = (unsigned int *)(entityData + c->offset);
		if(*index == UINT_MAX) {
			return NULL;
		}
		return (void *)((unsigned char *)c->data[*index]);
	}

	return (void *)(entityData + c->offset);
}

static void _removeComponentI(struct diana *diana, unsigned int entity, unsigned int component, unsigned int i) {
	unsigned char *entityData = _getEntityData(diana, entity);
	struct _component *c = diana->components + component;

	if(_denseIntegerSet_contains(diana, &diana->active, entity)) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if(!_bits_clear(entityData, component)) {
		return;
	}

	if(c->flags & DL_COMPONENT_MULTIPLE_BIT) {
		struct _componentBag *bag = (struct _componentBag *)(entityData + c->offset);
		if(i >= bag->count) {
			return;
		}
		_sparseIntegerSet_insert(diana, &c->freeDataIndexes, bag->indexes[i]);
		memcpy(bag->indexes + i, bag->indexes + i + 1, bag->count - i * sizeof(unsigned int));
		bag->indexes = _realloc(diana, bag->indexes, sizeof(unsigned int) * bag->count, sizeof(unsigned int) * (bag->count - 1));
		bag->count--;
		return;
	}

	if(c->flags & DL_COMPONENT_INDEXED_BIT) {
		unsigned int *index = (unsigned int *)(entityData + c->offset);
		_sparseIntegerSet_insert(diana, &c->freeDataIndexes, *index);
		*index = 0;
		return;
	}
}

unsigned int diana_clone(struct diana *diana, unsigned int parentEntity) {
	unsigned int newEntity, ci, cbi, cbn;
	struct _component *c;
	unsigned char *parentEntityData;

	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return 0;
	}

	if((!diana->processing && parentEntity >= diana->dataHeight) || (diana->processing && parentEntity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return 0;
	}

	newEntity = diana_spawn(diana);

	if(diana->error != DL_ERROR_NONE) {
		return 0;
	}

	parentEntityData = _getEntityData(diana, parentEntity);

	for(ci = 0; ci < diana->num_components; ci++) {
		if(!_bits_isSet(parentEntityData, ci)) {
			continue;
		}

		c = diana->components + ci;

		cbn = diana_getComponentCount(diana, parentEntity, ci);

		for(cbi = 0; cbi < cbn; cbi++) {
			void *cd = _getComponentI(diana, parentEntity, ci, cbi);
			_setComponentI(diana, newEntity, ci, cbi, cd);
		}
	}

	return newEntity;
}

// single
void diana_setComponent(struct diana *diana, unsigned int entity, unsigned int component, const void * data) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	_setComponentI(diana, entity, component, 0, data);
}

void * diana_getComponent(struct diana *diana, unsigned int entity, unsigned int component) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return NULL;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return NULL;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return NULL;
	}

	return _getComponentI(diana, entity, component, 0);
}

void diana_removeComponent(struct diana *diana, unsigned int entity, unsigned int component) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	_removeComponentI(diana, entity, component, 0);
}

// multiple
unsigned int diana_getComponentCount(struct diana *diana, unsigned int entity, unsigned int component) {
	unsigned char *entityData;
	struct _component *c;

	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return 0;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return 0;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return 0;
	}

	entityData = _getEntityData(diana, entity);
	c = diana->components + component;

	if(c->flags & DL_COMPONENT_MULTIPLE_BIT) {
		struct _componentBag *bag = (struct _componentBag *)(entityData + c->offset);
		return bag->count;
	} else {
		return _bits_isSet(entityData, component);
	}
}

void diana_appendComponent(struct diana *diana, unsigned int entity, unsigned int component, const void * data) {
	unsigned char *entityData;
	struct _component *c;

	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	entityData = _getEntityData(diana, entity);
	c = diana->components + component;

	if(c->flags & DL_COMPONENT_MULTIPLE_BIT) {
		_setComponentI(diana, entity, component, diana_getComponentCount(diana, entity, component), data);
	} else {
		_setComponentI(diana, entity, component, 0, data);
	}
}

void diana_removeComponents(struct diana *diana, unsigned int entity, unsigned int component) {
	unsigned char *entityData;
	struct _component *c;
	unsigned int i;

	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	entityData = _getEntityData(diana, entity);
	c = diana->components + component;

	if(c->flags & DL_COMPONENT_MULTIPLE_BIT) {
		struct _componentBag *bag = (struct _componentBag *)(entityData + c->offset);
		if(bag->count) {
			for(i = 0; i < bag->count; i++) {
				_sparseIntegerSet_insert(diana, &c->freeDataIndexes, bag->indexes[i]);
			}
			bag->count = 0;
			diana->free(bag->indexes);
			bag->indexes = NULL;
		}
	} else {
		_removeComponentI(diana, entity, component, 0);
	}
}

// low level
void diana_setComponentI(struct diana *diana, unsigned int entity, unsigned int component, unsigned int i, const void * data) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	_setComponentI(diana, entity, component, i, data);
}

void * diana_getComponentI(struct diana *diana, unsigned int entity, unsigned int component, unsigned int i) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return NULL;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return NULL;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return NULL;
	}

	return _getComponentI(diana, entity, component, i);
}

void diana_removeComponentI(struct diana *diana, unsigned int entity, unsigned int component, unsigned int i) {
	if(!diana->initialized) {
		diana->error = DL_ERROR_INVALID_OPERATION;
		return;
	}

	if((!diana->processing && entity >= diana->dataHeight) || (diana->processing && entity >= diana->dataHeightCapacity + diana->processingDataHeight)) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	if(component >= diana->num_components) {
		diana->error = DL_ERROR_INVALID_VALUE;
		return;
	}

	_removeComponentI(diana, entity, component, i);
}

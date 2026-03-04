#ifndef __CORE_LIST_H__
#define __CORE_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define LIST_POISON1 NULL
#define LIST_POISON2 NULL

#ifndef offsetof
#define offsetof(type, member) (size_t)(&((type *)0)->member)
#endif

#define container_of(ptr, type, member)                                                               \
    ({                                                                                                 \
        const typeof(((type *)0)->member) *__mptr = (ptr);                                            \
        (type *)((char *)__mptr - offsetof(type, member));                                             \
    })
#define CONTAINER_OF(ptr, type, member) container_of(ptr, type, member)

typedef struct ListHead {
    struct ListHead *prev;
    struct ListHead *next;
} ListHead;

#define LIST_HEAD_INIT(name) \
    { &(name), &(name) }
#define LIST_HEAD(name) ListHead name = LIST_HEAD_INIT(name)

static inline void init_list_head(ListHead *list)
{
    list->next = list;
    list->prev = list;
}
#define INIT_LIST_HEAD(list) init_list_head(list)

static inline void list_add_between(ListHead *new_node, ListHead *prev, ListHead *next)
{
    new_node->prev = prev;
    prev->next = new_node;
    new_node->next = next;
    next->prev = new_node;
}

static inline void list_add(ListHead *new_node, ListHead *head)
{
    list_add_between(new_node, head, head->next);
}

static inline void list_add_tail(ListHead *new_node, ListHead *head)
{
    list_add_between(new_node, head->prev, head);
}

static inline void list_del_between(ListHead *prev, ListHead *next)
{
    prev->next = next;
    next->prev = prev;
}

static inline void list_del(ListHead *entry)
{
    list_del_between(entry->prev, entry->next);
    entry->next = entry;
    entry->prev = entry;
}

static inline uint8_t list_empty(const ListHead *head)
{
    return (head->next == head);
}

static inline void list_splice_between(ListHead *list, ListHead *head)
{
    ListHead *first = list->next;
    ListHead *last = list->prev;
    ListHead *at = head->next;
    first->prev = head;
    head->next = first;
    last->next = at;
    at->prev = last;
}

static inline void list_splice(ListHead *list, ListHead *head)
{
    if (!list_empty(list)) {
        list_splice_between(list, head);
    }
}

static inline void list_replace(ListHead *old_node, ListHead *new_node)
{
    new_node->next = old_node->next;
    new_node->next->prev = new_node;
    new_node->prev = old_node->prev;
    new_node->prev->next = new_node;
}

static inline void list_replace_init(ListHead *old_node, ListHead *new_node)
{
    list_replace(old_node, new_node);
    init_list_head(old_node);
}

static inline void list_move(ListHead *list, ListHead *head)
{
    list_del_between(list->prev, list->next);
    list_add(list, head);
}

static inline void list_move_tail(ListHead *list, ListHead *head)
{
    list_del_between(list->prev, list->next);
    list_add_tail(list, head);
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
#define list_for_each_prev(pos, head) for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each_entry(pos, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member); &pos->member != (head); pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member), n = list_entry(pos->member.next, typeof(*pos), member); &pos->member != (head); pos = n, n = list_entry(n->member.next, typeof(*n), member))

/* Compatibility aliases for existing uppercase call sites. */
#define LIST_ENTRY(ptr, type, member) list_entry(ptr, type, member)
#define LIST_FIRST_ENTRY(ptr, type, member) list_first_entry(ptr, type, member)
#define LIST_FOR_EACH(pos, head) list_for_each(pos, head)
#define LIST_FOR_EACH_SAFE(pos, n, head) list_for_each_safe(pos, n, head)
#define LIST_FOR_EACH_PREV(pos, head) list_for_each_prev(pos, head)
#define LIST_FOR_EACH_ENTRY(pos, head, member) list_for_each_entry(pos, head, member)
#define LIST_FOR_EACH_ENTRY_SAFE(pos, n, head, member) list_for_each_entry_safe(pos, n, head, member)

#ifdef __cplusplus
}
#endif

#endif

//
// Created by mabaiming on 17-10-11.
//

#ifndef TCP_TUNNEL_LIST_H
#define TCP_TUNNEL_LIST_H

typedef struct Iterator Iterator;
typedef struct List List;
typedef bool (*EqF)(void* a, void* b);
typedef void (*DataDo)(void* a);

struct Iterator {
  void* data;
  Iterator* pre;
  Iterator* next;
  List* list;
};

struct List {
  Iterator* first;
  Iterator* last;
  int size;
};

bool
iteratorRemove(Iterator* it) {
  if (it == NULL) {
    return false;
  }
  List* list = it->list;
  if (it->pre) {
    it->pre->next = it->next;
  } else {
    list->first = it->next;
  }
  if (it->next) {
    it->next->pre = it->pre;
  } else {
    list->last = it->pre;
  }
  --it->list->size;
  free(it);
  return true;
}

bool
iteratorMove(Iterator* it, List* list2) {
  if (it == NULL) {
    return false;
  }
  List* list = it->list;
  if (it->pre) {
    it->pre->next = it->next;
  } else {
    list->first = it->next;
  }
  if (it->next) {
    it->next->pre = it->pre;
  } else {
    list->last = it->pre;
  }
  --it->list->size;
  if (list2->size == 0) {
    list->first = it;
    list->last = it;
  } else {
    it->pre = list->last;
    list->last->next = it;
    list->last = it;
    it->list = list;
  }
  ++list2->size;
  return true;
}

List*
listNew() {
  List* list = malloc(sizeof(List));
  list->first = NULL;
  list->last = NULL;
  list->size = 0;
  return list;
}

Iterator*
listGet(List* list, void* data, EqF f) {
  Iterator* it = list->first;
  while (it != NULL) {
    if (f(it->data, data)) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

Iterator*
listAdd(List* list, void* data) {
  Iterator* it = malloc(sizeof(Iterator));
  it->data = data;
  it->next = NULL;
  it->pre = NULL;
  it->list = list;
  if (list->size == 0) {
    list->first = it;
    list->last = it;
  } else {
    it->pre = list->last;
    list->last->next = it;
    list->last = it;
  }
  ++list->size;
  return it;
}

bool
listRemove(List* list, void* data, EqF f) {
  Iterator* it = listGet(list, data, f);
  return iteratorRemove(it);
}

void
listForeach(List* list, DataDo dataDo) {
  Iterator* it = list->first;
  while (it != NULL) {
    dataDo(it->data);
    it = it->next;
  }
}

void
listClear(List* list) {
  Iterator* it = list->first;
  while (it != NULL) {
    Iterator* t = it->next;
    free(it);
    it = t;
  }
}

#endif //TCP_TUNNEL_LIST_H

#include <stdlib.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
	free(pointer);
	return NULL;
    }

    void *result = realloc(pointer, newSize);
    if (result == NULL) {
	exit(1);
    }

    return result;
}

static void freeObject(Obj *object) {
    switch (object->type) {
    case OBJ_CLOSURE: {
	// ObjFunction は複数の ObjClosure から共有され得るため解放しない。
	// upvalues 配列 (ポインタの配列) 自体は closure が所有するので解放するが、
	// 配列が指す各 ObjUpvalue は他の closure とも共有され得るため
	// ここでは解放しない (それぞれ独立した Obj として GC 対象になる)。
	ObjClosure *closure = (ObjClosure *)object;
	FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);
	FREE(ObjClosure, object);
	break;
    }
    case OBJ_FUNCTION: {
	ObjFunction *function = (ObjFunction *)object;
	freeChunk(&function->chunk);
	FREE(ObjFunction, object);
	break;
    }
    case OBJ_NATIVE:
	FREE(ObjNative, object);
	break;
    case OBJ_STRING: {
	ObjString *string = (ObjString *)object;
	FREE_ARRAY(char, string->chars, string->length + 1);
	FREE(ObjString, object);
	break;
    }
    case OBJ_UPVALUE:
	// location はスタック上のアドレスを指すだけで所有していないため解放しない。
	FREE(ObjUpvalue, object);
	break;
    }
}

void freeObjects() {
    Obj *object = vm.objects;

    while (object != NULL) {
	Obj *next = object->next;
	freeObject(object);
	object = next;
    }
}

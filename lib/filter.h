#ifndef SHOVELER_FILTER_H
#define SHOVELER_FILTER_H

#include "texture.h"
#include "types.h"
#include "uniform_map.h"

struct ShovelerFilterStruct;

typedef int (ShovelerFilterFilteringFunction)(struct ShovelerFilterStruct *filter);
typedef void (ShovelerFilterFreeDataFunction)(void *data);

typedef struct ShovelerFilterStruct {
	ShovelerTexture *inputTexture;
	ShovelerTexture *outputTexture;
	void *data;
	ShovelerFilterFilteringFunction *filterTexture;
	ShovelerFilterFreeDataFunction *freeData;
} ShovelerFilter;

static inline int shovelerFilterRender(ShovelerFilter *filter, ShovelerTexture *inputTexture)
{
	filter->inputTexture = inputTexture;
	return filter->filterTexture(filter);
}

static inline void shovelerFilterFree(ShovelerFilter *filter)
{
	filter->freeData(filter->data);
}

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "libcoro.h"

struct my_context {
    char *name;             // Имя контекста
    char **files;           // Массив имен файлов для обработки
    int numFiles;           // Количество файлов
    int *fileIndex;         // Указатель на текущий индекс файла в массиве
    int **dataPtrArray;     // Указатель на массив указателей на данные каждого файла
    int *dataArray;         // Указатель на массив данных текущего файла
    int *sizePtrArray;      // Указатель на массив размеров данных каждого файла
    int totalTimeSec;       // Общее время выполнения в секундах
    int totalTimeNsec;      // Общее время выполнения в наносекундах
    int startTimeSec;       // Время начала выполнения в секундах
    int startTimeNsec;      // Время начала выполнения в наносекундах
    int finishTimeSec;      // Время окончания выполнения в секундах
    int finishTimeNsec;     // Время окончания выполнения в наносекундах
    int timeLimitNsec;      // Предел времени выполнения в наносекундах
};

typedef struct {
    int fileCount;
    int coroutineCount;
} CommandLineArgs;

void swap(int *a, int *b) {
	int t = *a;
	*a = *b;
	*b = t;
}

static struct my_context *threadContextCreate(const char *name, char **files, int numFiles, int *index, int **ptrArray, int* sizeArray, int limit) {
	struct my_context *context = malloc(sizeof(*context));
	context->name = strdup(name);
	context->files = files;
	context->fileIndex = index;
	context->numFiles = numFiles;
	context->dataPtrArray = ptrArray;
	context->sizePtrArray = sizeArray;
	context->timeLimitNsec = limit;
	return context;
}

static void threadContextDestroy(struct my_context *context) {
	free(context->name);
	free(context);
}

static void stopTimer(struct my_context *context) {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	context->finishTimeSec = time.tv_sec;
	context->finishTimeNsec = time.tv_nsec;
}

static void startTimer(struct my_context *context) {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	context->startTimeSec = time.tv_sec;
	context->startTimeNsec = time.tv_nsec;
}

static void updateTimeTotal(struct my_context *context) {
    long secDiff = context->finishTimeSec - context->startTimeSec;
    long nsecDiff = context->finishTimeNsec - context->startTimeNsec;

    if (nsecDiff < 0) {
        secDiff--;
        nsecDiff += 1000000000;
    }

    context->totalTimeSec += secDiff;
    context->totalTimeNsec += nsecDiff;
}

int partition(int *array, int left, int right) {
	int pivot = array[right];
	int i = (left - 1);

	for (int j = left; j < right; j++) {
		if (array[j] <= pivot) {
			i++;
			swap(&array[i], &array[j]);
		}
	}
	swap(&array[i + 1], &array[right]);
	return (i + 1);
}

static void checkExceedAndYield(struct my_context *context) {
    stopTimer(context);
    int currentQuantum = (context->finishTimeSec - context->startTimeSec) * 1000000000 + (context->finishTimeNsec - context->startTimeNsec);
    if (currentQuantum > context->timeLimitNsec) {
        updateTimeTotal(context);
        coro_yield();
        startTimer(context);
    }
}

void quickSort(int *array, int left, int right, struct my_context *context) {
	if (left < right) {
		int pi = partition(array, left, right);
		quickSort(array, left, pi - 1, context);
		quickSort(array, pi + 1, right, context);

		checkExceedAndYield(context);
	}
}

// Функция для чтения данных из файла
int readData(FILE *file, int **data_array) {
    int data_capacity = 100;
    int *data = malloc(data_capacity * sizeof(int));
    if (!data) {
        printf("Memory allocation error\n");
        return -1;
    };

    int data_size = 0;
    int value;
    while (fscanf(file, "%d", &value) == 1) {
        if (data_size == data_capacity) {
            data_capacity *= 2;
            int *temp = realloc(data, data_capacity * sizeof(int));
            if (!temp) {
                printf("Memory reallocation error\n");
                free(data);
                return 0;
            }
            data = temp;
        }
        data[data_size++] = value;
    }

    *data_array = realloc(data, data_size * sizeof(int));
    return (*data_array) ? data_size : 0;
}

int processFile(struct my_context *ctx, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error opening file: %s\n", filename);
        return -1;
    }

    int *data;
    int size = readData(file, &data);
    fclose(file);

    if (!size){
        printf("Error reading file: %s\n", filename);
        return -1;
    };

    ctx->dataPtrArray[*ctx->fileIndex] = data;
    ctx->sizePtrArray[*ctx->fileIndex] = size;
    (*ctx->fileIndex)++;

    quickSort(data, 0, size - 1, ctx);
    return 0;
}

static int coroutineFunction(void *context) {
    struct coro *cr = coro_this();
    struct my_context *ctx = context;
    startTimer(ctx);

    while (*ctx->fileIndex != ctx->numFiles) {
        const char *filename = ctx->files[*ctx->fileIndex];
        int result = processFile(ctx, filename);
        if (result != 0) {
            printf("Error \n");
            threadContextDestroy(ctx);
            return result;
        }
    }

    stopTimer(ctx);
    updateTimeTotal(ctx);

    long long int switchCount = coro_switch_count(cr);
    int totalTimeUs = ctx->totalTimeSec * 1000000 + ctx->totalTimeNsec / 1000;

    printf("[%s]: switch %lld,", ctx->name, switchCount);
    printf("time %d us\n", totalTimeUs);

    threadContextDestroy(ctx);
    return 0;
}

int merge(int **data, int *size, int *idx, int cnt) {
    int minIdx = -1;
    int currMin = INT_MAX;
    for (int i = 0; i < cnt; ++i) {
        if ((size[i] > idx[i]) && (data[i][idx[i]] < currMin)) {
            currMin = data[i][idx[i]];
            minIdx = i;
        }
    }
    return minIdx;
}

void mergeAndPrint(FILE *out, int **dataArrays, int *dataArraySizes, int *dataArrayReadIndices, int fileCount) {
    int minIdx = 0;
    while (minIdx != -1) {
        minIdx = merge(dataArrays, dataArraySizes, dataArrayReadIndices, fileCount);
        if (minIdx != -1) {
            fprintf(out, "%d ", dataArrays[minIdx][dataArrayReadIndices[minIdx]]);
            dataArrayReadIndices[minIdx] += 1;
        }
    }
}

int parseCommandLine(int argc, char **argv, CommandLineArgs *args) {
    if (argc < 4) {
        printf("Error! Enter valid values.\n");
        return -1;
    }

    char *endptr;
    args->coroutineCount = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || args->coroutineCount <= 0) {
        printf("Error! Enter a valid number of coroutines.\n");
        return -1;
    }

    args->fileCount = argc - 3;

    if (args->fileCount <= 0) {
        printf("Error! Enter valid file names.\n");
        return -1;
    }

    return 2;
}

int main(int argc, char **argv) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    CommandLineArgs commandLineArgs;
    if (!parseCommandLine(argc, argv, &commandLineArgs)) {
        printf("Parse Error! \n");
        return -1;
    }

    coro_sched_init();

    int fileCount = commandLineArgs.fileCount;
    int coroutineCount = commandLineArgs.coroutineCount;

    int *dataArrays[fileCount];
    int dataArraySizes[fileCount];
    int dataArrayReadIndices[fileCount];
    int fileIdx = 0;

    for (int i = 0; i < coroutineCount; ++i) {
        char name[16];
        sprintf(name, "coro_%d", i);
        coro_new(coroutineFunction,
                 threadContextCreate(name, argv + 3, fileCount, &fileIdx, dataArrays, dataArraySizes,
                 atoi(argv[1]) * 1000 / fileCount));
    }
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        coro_delete(c);
    }

    for (int i = 0; i < fileCount; ++i) {
        dataArrayReadIndices[i] = 0;
    }

    FILE *out = fopen("out.txt", "w");

    mergeAndPrint(out, dataArrays, dataArraySizes, dataArrayReadIndices, fileCount);

    fclose(out);

    for (int i = 0; i < fileCount; ++i) {
        free(dataArrays[i]);
    }

    struct timespec finish;
    clock_gettime(CLOCK_MONOTONIC, &finish);

    printf("Total time: %ld us\n",
           (finish.tv_sec - start.tv_sec) * 1000000 + (finish.tv_nsec - start.tv_nsec) / 1000);

    return 0;
}

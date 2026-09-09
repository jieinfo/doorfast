#ifndef DOORFAST_H
#define DOORFAST_H

#define DF_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

enum df_result {
    DF_OK = 0,
    DF_ERR_INVALID = 1,
    DF_ERR_IO = 2,
};

#endif

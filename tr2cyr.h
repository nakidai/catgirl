#ifndef __TR2CYR_H__
#define __TR2CYR_H__

#include <stddef.h>
#include <wchar.h>


typedef int Translator_Writer(wchar_t ch, void *arg);
typedef wint_t Translator_Reader(size_t i, void *arg);

int Translator_convert(Translator_Reader *reader, void *readerarg, Translator_Writer *writer, void *writerarg);

#endif /* __TR2CYR_H__ */

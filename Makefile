SHARED := -shared
TARGET := uw8_libretro.so

ifeq ($(shell uname -s),) # win
	SHARED := -shared
	TARGET := uw8_libretro.dll
else ifneq ($(findstring MINGW,$(shell uname -s)),) # win
	SHARED := -shared
	TARGET := uw8_libretro.dll
else ifneq ($(findstring Darwin,$(shell uname -s)),) # osx
	SHARED := -dynamiclib
	TARGET := uw8_libretro.dylib
endif

CFLAGS += -O3 -fPIC -flto -L.

OBJ = uw8.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJ)
	$(CC) $(SHARED) -o $@ $^ $(CFLAGS) -lm3

clean:
	rm $(OBJ) $(TARGET)

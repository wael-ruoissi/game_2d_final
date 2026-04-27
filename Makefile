# اسم البرنامج اللي باش يخرج في الأخير
EXEC = my_game

# المترجم
CC = gcc

# المجلدات
SRC_DIR = src
INC_DIR = include

# ملفات الكود (.c)
# يلوج على كل ملف .c في مجلد src
SRCS = $(wildcard $(SRC_DIR)/*.c)

# خيارات الـ Compile (وين يلقى الـ .h والـ Warnings)
CFLAGS = -I$(INC_DIR) -Wall -Wextra

# المكتبات متاع SDL2 (لازمين باش اللعبة تخدم)
LDFLAGS = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm

# القاعدة الأساسية
all:
	$(CC) $(SRCS) -o $(EXEC) $(CFLAGS) $(LDFLAGS)

# قاعدة لتنظيف الملفات الزايدة
clean:
	rm -rf $(EXEC)

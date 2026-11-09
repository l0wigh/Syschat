BLU			= \033[0;34m
GRN			= \033[0;32m
RED			= \033[0;31m
RST			= \033[0m
END			= \e[0m
TOTEM 		= 🦁

FILES = syschat \
		utils/network \
		utils/error \
		utils/commands \
		utils/server \
		utils/buffers \
		utils/screen \
		utils/ctcp \
		segfault_catcher/segfault_catcher \

SRCS = $(FILES:%=srcs/%.c)
NAME		= syschat
OBJS_DIR	= objs/
PROJECT_H	= srcs/
OBJS		= $(SRCS:.c=.o)
OBJECTS_PREFIXED = $(addprefix $(OBJS_DIR), $(OBJS))

# mbedTLS is vendored (vendor/mbedtls) and built with the same compiler as
# syschat: no system libssl to install, no libc mismatch. Third-party code is
# built without -Werror and without sanitizers.
MBEDTLS_DIR		= vendor/mbedtls
MBEDTLS_OBJS_DIR	= build/mbedtls/
MBEDTLS_INC		= -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library
MBEDTLS_SRCS	= $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJS	= $(patsubst $(MBEDTLS_DIR)/library/%.c,$(MBEDTLS_OBJS_DIR)%.o,$(MBEDTLS_SRCS))
CC			?= gcc
CC_FLAGS	= -std=c11 -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -O2
# CC_FLAGS	= -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -g3
# CC_FLAGS	= -Wall -Werror -Wextra -g3
LIBS		=

$(OBJS_DIR)%.o : %.c $(PROJECT_H)
	@mkdir -p $(dir $@)
	@$(CC) $(CC_FLAGS) $(MBEDTLS_INC) -c $< -o $@
	@printf	"\033[2K\r${BLU}${TOTEM} [BUILD]${RST} '$<' $(END)"

$(MBEDTLS_OBJS_DIR)%.o : $(MBEDTLS_DIR)/library/%.c
	@mkdir -p $(dir $@)
	@$(CC) -std=c11 -O2 -w $(MBEDTLS_INC) -c $< -o $@

$(NAME): $(OBJECTS_PREFIXED) $(MBEDTLS_OBJS)
	@$(CC) -o $(NAME) $(OBJECTS_PREFIXED) $(MBEDTLS_OBJS) $(CC_FLAGS) $(MBEDTLS_INC) $(LIBS)
	@printf "\033[2K\r\033[0;32m${TOTEM} [END]\033[0m $(NAME)$(END)\n"

all: $(NAME)

clean:
	@rm -rf $(OBJS_DIR)
	@rm -rf build
	@printf "\033[2K\r${GRN}${TOTEM} [CLEAN]${RST} done$(END)\n"

fclean: clean
	@rm -f $(NAME)
	@rm -rf $(OBJS_DIR)
	@printf "\033[2K\r${GRN}${TOTEM} [FCLEAN]${RST} done$(END)\n"

re: fclean all

install: re
	@sudo cp syschat /usr/local/bin/syschat
	@printf "\033[2K\r${GRN}${TOTEM} [INSTALL]${RST} done$(END)\n"

uninstall: fclean
	@sudo rm /usr/local/bin/syschat
	@printf "\033[2K\r${GRN}${TOTEM} [UNINSTALL]${RST} done$(END)\n"

.PHONY:		all clean fclean re install uninstall

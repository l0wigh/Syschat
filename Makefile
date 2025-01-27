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

SRCS = $(FILES:%=srcs/%.c)
NAME		= syschat
OBJS_DIR	= objs/
PROJECT_H	= srcs/
OBJS		= $(SRCS:.c=.o)
OBJECTS_PREFIXED = $(addprefix $(OBJS_DIR), $(OBJS))
CC			= gcc
CC_FLAGS	= -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -O2 -flto
CC_FLAGS	= -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -g3
# CC_FLAGS	= -Wall -Werror -Wextra -g3
LIBS		=

$(OBJS_DIR)%.o : %.c $(PROJECT_H)
	@mkdir -p $(dir $@)
	@$(CC) $(CC_FLAGS) -c $< -o $@
	@printf	"\033[2K\r${BLU}${TOTEM} [BUILD]${RST} '$<' $(END)"

$(NAME): $(OBJECTS_PREFIXED)
	@$(CC) -o $(NAME) $(OBJECTS_PREFIXED) $(CC_FLAGS) $(LIBS)
	@printf "\033[2K\r\033[0;32m${TOTEM} [END]\033[0m $(NAME)$(END)\n"

all: $(NAME)

clean:
	@rm -rf $(OBJS_DIR)
	@printf "\033[2K\r${GRN}${TOTEM} [CLEAN]${RST} done$(END)\n"

fclean: clean
	@rm -f $(NAME)
	@rm -rf $(OBJS_DIR)
	@printf "\033[2K\r${GRN}${TOTEM} [FCLEAN]${RST} done$(END)\n"

re: fclean all

.PHONY:		all clean fclean re

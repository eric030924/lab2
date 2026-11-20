#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdio.h>

/* 공동 함수 */
int copy_stream(FILE *src, FILE *dst);
char *join_path(const char *directory, const char *name);

/* 임채민 담당: cat, grep, pwd */
int cmd_cat(char *argv[]);
int cmd_grep(char *argv[]);
int cmd_pwd(char *argv[]);

/* 박재석 담당: ls, mkdir, rmdir, rm */
int cmd_ls(char *argv[]);
int change_paths(char *argv[]);

/* 김시현 담당: cd, cp, mv, ln */
int cmd_cd(char *argv[]);
int cmd_cp(char *argv[]);
int cmd_mv(char *argv[]);
int cmd_ln(char *argv[]);

#endif

#define _XOPEN_SOURCE 500 // for nftw()

#include <ftw.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plugin_api.h"

int is_debug = 0;

int flag_A = 1; // default
int flag_O = 0;
int flag_N = 0;

// struct option *longopts = NULL;
int longopts_size = 1; //для последней опции, которая равна {0, 0, 0, 0}
int *id_actual_opts = NULL;
void **lib_holders = NULL;
char *tmp_path = NULL;

char **ok_files = NULL;
int ok_files_size = 0;

char **tmp = NULL;
int tmp_size = 0;

int first_time = 1;

void join_to_ok_files(char ***ok_files, int *ok_files_size,
                      const char **fpath) {

  *ok_files_size = *ok_files_size + 1;
  *ok_files = (char **)realloc(*ok_files, (*ok_files_size) * sizeof(char *));
  if (!(*ok_files)) {
    fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
    return;
  }

  (*ok_files)[*ok_files_size - 1] = strdup(*fpath);
}

struct option *opts_to_pass = NULL;
int opts_to_pass_len = 1;
typedef int (*ppf_func_t)(const char *, struct option *, size_t);
ppf_func_t ppf_func = NULL;

int count_ok_file = 0;

int walk_func(const char *fpath, const struct stat *sb, int typeflag,
              struct FTW *ftwbuf) {
  if (sb == NULL || ftwbuf == NULL)
    return 0;

  int ret;
  if (typeflag == FTW_F) {

    ret = ppf_func(fpath, opts_to_pass, opts_to_pass_len);

    if (is_debug)
      fprintf(stderr, "DEBUG: The plagin's function returns: %d\n ", ret);

    /*
res = 1 если файл нам подходит:
    если флаг N = 0, то подходящими являются файлы с ret = 0
    если флаг N = 1, то подходящими считаются фалйы с ret = 1
    */

    int res = !(ret ^ flag_N);

    //количество подходящих файлов для этой опции
    if (res)
      count_ok_file++;

    if (first_time | ((!(first_time)) & flag_O & !ok_files)) {

      if (res) {
        join_to_ok_files(&ok_files, &ok_files_size, &fpath);
      }

      return 0;
    }

    int count = 0;

    for (int i = 0; i < ok_files_size; i++) {

      //проверка, есть ли такой файл  уже
      int strcmp_check = strcmp(ok_files[i], fpath);

      // если нам этот файл подходит и включен флаг ИЛИ
      // и... (ниже)
      if ((strcmp_check != 0) && flag_O && res) {
        count++;
      }

      //...и такого файла еще нетто добавим его в массив
      if (count == ok_files_size)
        join_to_ok_files(&ok_files, &ok_files_size, &fpath);

      //если нам этот файл подходит и включен флаг И
      //если такой файл уже есть, то создаем новый массив структур вместе с
      //ним

      if ((strcmp_check == 0) && flag_A && res) {
        join_to_ok_files(&tmp, &tmp_size, &fpath);
      }
    }
  }

  return 0;
};

void version() {
  fprintf(stdout, ""
                  "Version: 0.0.2\n"
                  "Author: Artemenko Svetlana\n"
                  "Group: N3251\n"
                  "Variant: 1\n");
}

// ---------------------------------------------------------------MAIN
int main(int argc, char *argv[]) {
  struct option *longopts = NULL;

  char *debug_var = getenv("LAB1DEBUG");
  if (debug_var) {
    is_debug = atoi(debug_var);
  }
  is_debug = is_debug == 1 ? 1 : 0;

  char *fpath = NULL;

  if (argc > 1) {
    fpath = strdup(argv[argc - 1]);
  }

  // ---------------------------------------------------------------ИНИЦИАЛИЗАЦИЯ
  // ПУТИ ДЛЯ ДАЛЬНЕЙШЕГО ПОИСКА ПЛАГИНОВ

  char *check_is_lib_path = NULL;

  //если был задан флаг -P вручную проверка флага -Р
  for (int i = 0; i < argc; i++) {
    if (!strcmp("-P", argv[i])) {
      char *path = argv[i + 1];

      check_is_lib_path = malloc((strlen(path) + 1) * sizeof(char));

      if (!check_is_lib_path) {
        fprintf(stderr, "ERROR: malloc() failed: %s\n", strerror(errno));
        goto END;
      }

      strcpy(check_is_lib_path, path);
    }
  }

  //если это текущий путь
  DIR *d;
  struct dirent *dir;

  if (strstr(argv[0], "./")) {

    char *cwd = malloc(1024 * 1024); // max dir length must be less than 1MB
    cwd = getcwd(cwd, 1024 * 1024);
    if (!cwd) {
      fprintf(stderr, "ERROR: getcwd failed: %d\n", errno);
      goto END;
    }

    check_is_lib_path = strdup(cwd);
    free(cwd);

  } else {
    char *last_slash = strrchr(argv[0], '/');

    char *crutch = strdup(argv[0]);
    int len = strlen(crutch) - strlen(last_slash);

    check_is_lib_path = malloc((len + 1) * sizeof(char));

    if (!check_is_lib_path) {
      fprintf(stderr, "ERROR: calloc() failed: %s\n", strerror(errno));
      goto END;
    }

    check_is_lib_path = strncpy(check_is_lib_path, argv[0], len * sizeof(char));

    free(crutch);
  }

  d = opendir(check_is_lib_path);

  if (!d) {
    fprintf(stderr, "ERROR: opendir() failed: \n");
    goto END;
  }

  if (is_debug)
    fprintf(stdout, "DEBUG: libs' path is %s\n", check_is_lib_path);

  int offset = strlen(check_is_lib_path);
  check_is_lib_path = realloc(check_is_lib_path, (offset + 2) * sizeof(char));
  if (!check_is_lib_path) {
    fprintf(stderr, "ERROR: relloc() failed: %s\n", strerror(errno));
    goto END;
  }

  strcat(check_is_lib_path, "/");
  offset = strlen(check_is_lib_path);

  // --------------------------------------------------------------- LONGOPTS

  int ret;

  struct plugin_info pi = {0};
  void *dl = NULL;
  typedef int (*pgi_func_t)(struct plugin_info *);
  void *func = NULL;

  int number = 0;

  while ((dir = readdir(d)) != NULL) {
    check_is_lib_path =
        realloc(check_is_lib_path, offset + sizeof(dir->d_name));

    if (!check_is_lib_path) {
      fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
      goto END;
    }

    memcpy(check_is_lib_path + offset, dir->d_name, sizeof(dir->d_name));

    // поиск первого вхождения точки и если после нее so,
    //     то пытаемся открыть как библиотеку
    //

    tmp_path = strstr(check_is_lib_path, ".");
    if (!tmp_path) {
      //значит там даже точек нет
      continue;
    }
    tmp_path = strstr(tmp_path, "so");
    if (!tmp_path) {
      //значит после точки нет so
      continue;
    }

    dl = dlopen(check_is_lib_path, RTLD_LAZY);
    if (!dl) {
      fprintf(stderr,
              "ERROR: dlopen () failed: %s  (is it really a library?)\n",
              dlerror());
      goto END;
    }
    if (is_debug)
      fprintf(stdout, "DEBUG: dl is open\n");

    func = dlsym(dl, "plugin_get_info");
    if (!func) {
      fprintf(stderr, "ERROR: dlsym() failed: %s\n", dlerror());
      goto END;
    }

    pgi_func_t pgi_func = (pgi_func_t)func;

    ret = pgi_func(&pi);

    if (is_debug) {
      fprintf(stdout, "DEBUG: Plugin purpose: %s\n", pi.plugin_purpose);
      fprintf(stdout, "DEBUG: Plugin author: %s\n", pi.plugin_author);
      fprintf(stdout, "DEBUG: Supported options length: %d\n",
              (int)pi.sup_opts_len);
    }
    if (ret < 0) {
      fprintf(stderr, "ERROR: plugin_get_info() failed\n");
      goto END;
    }
    longopts_size = longopts_size + pi.sup_opts_len;

    longopts = (struct option *)realloc(longopts,
                                        sizeof(struct option) * longopts_size);
    if (!longopts) {
      fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
      goto END;
    }

    lib_holders =
        (void **)realloc(lib_holders, sizeof(void *) * (longopts_size - 1));

    if (!lib_holders) {
      fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
      goto END;
    }

    if (argc == 1 || argc == 3) {
      fprintf(stdout, "\nPlugin purpose:\t\t%s\n", pi.plugin_purpose);
      fprintf(stdout, "Plugin author:\t\t%s\n", pi.plugin_author);
      fprintf(stdout, "Supported options: ");
      if (pi.sup_opts_len > 0) {
        fprintf(stdout, "\n");
        for (size_t i = 0; i < pi.sup_opts_len; i++) {
          fprintf(stdout, "\t--%s\t\t%s\n", pi.sup_opts[i].opt.name,
                  pi.sup_opts[i].opt_descr);
        }
      }
    }

    for (size_t i = 0; i < pi.sup_opts_len; i++) {

      memcpy(longopts + number, &pi.sup_opts[i].opt, sizeof(struct option));

      lib_holders[number] = dl;

      number++;
    }

    func = NULL;
  }

  closedir(d);

  //Последний элемент массива должен быть заполнен нулями.
  if (!longopts) {
    fprintf(stderr, "ERROR: There are not any plugins.\n");
    goto END;
  }

  if (is_debug) {
    for (int i = 0; i < longopts_size - 1; i++)
      fprintf(stdout, "DEBUG: Opt %d name: %s; has_arg: %d; val: %d(%c)\n", i,
              longopts[i].name, longopts[i].has_arg, longopts[i].val,
              longopts[i].val);
  }

  struct option last_el = {0, 0, 0, 0};
  memcpy(longopts + number, &last_el, sizeof(struct option));

  // --------------------------------------------НАЧАЛО ПАРСИНГА ОПЦИЙ

  int id_actual_opts_len = 0;

  int c;
  int option_index = 0;

  while (1) {

    c = getopt_long(argc, argv, "P:AONhv", longopts, &option_index);

    if (c == -1)
      break;

    // printf("%d   %s     %d       %s\n", optind,
    // longopts[option_index].name,
    //        option_index, lib_paths[option_index].path);

    // flag_AO = 1 значит логическое И
    // flag_AO = 0 значит логическое ИЛИ
    // flag_N = 1, значит инвертируем

    switch (c) {
    case 'P':
      break;
    case 'A':
      flag_A = 1;
      break;
    case 'O':
      flag_O = 1;
      flag_A = 0;
      break;
    case 'N':
      flag_N = 1;
      break;
    case 'v':
      version();
      goto END;
      break;
    case 'h':
      fprintf(stdout,
              "Usage:\nlab1saa [[option] dir]\nOption:\n -P <dir>     Full "
              "path to "
              "plugin directory\n -A          And operator\n -O           Or "
              "operator\n -N           Not operator\n -v           Display "
              "version "
              "of "
              "programm\n -h           Display help for options\n");
      break;
    case '?':
      fprintf(stdout, "failed to recognize option: %d\n", optopt);
      break;

    case 0:

      //количество актуальных опций
      id_actual_opts_len++;
      id_actual_opts =
          realloc(id_actual_opts, id_actual_opts_len * sizeof(int));

      if (!id_actual_opts) {
        fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
        goto END;
      }

      memcpy(id_actual_opts + id_actual_opts_len - 1, &option_index,
             sizeof(int));
      //если есть аргумент, то мы его записываем в longopts
      if ((longopts + option_index)->has_arg) {
        (longopts + option_index)->flag = (int *)strdup(optarg);
      }

      break;
    }
  }

  if (flag_N) {
    flag_A = !flag_A;
    flag_O = !flag_O;
  }

  // ------------------------------------------ОБРАБОТЧИК АКТУАЛЬНЫХ ОПЦИЙ

  void *current_dl = NULL;
  void *next_dl = NULL;
  struct option *current_opt = NULL;
  struct option *next_opt = NULL;

  for (int i = 0; i < id_actual_opts_len; i++) {
    //если опцию уже проверили
    if (id_actual_opts[i] == -1)
      break;

    //если это новая опция, то обновляем opts_to_pass_len
    opts_to_pass_len = 1;

    int *current_id = &id_actual_opts[i];
    current_opt = &longopts[*current_id];
    current_dl = lib_holders[*current_id];

    opts_to_pass =
        realloc(opts_to_pass, opts_to_pass_len * sizeof(struct option));

    if (!opts_to_pass) {
      fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
      goto END;
    }
    memcpy(opts_to_pass + opts_to_pass_len - 1, current_opt,
           sizeof(struct option));

    for (int j = i + 1; j < id_actual_opts_len; j++) {
      if (id_actual_opts[j] == -1)
        break;

      int *next_id = &id_actual_opts[j];
      next_opt = &longopts[*next_id];
      next_dl = lib_holders[*next_id];

      if (next_dl == current_dl) {
        opts_to_pass_len++;
        opts_to_pass =
            realloc(opts_to_pass, opts_to_pass_len * sizeof(struct option));
        if (!opts_to_pass) {
          fprintf(stderr, "ERROR: realloc() failed: %s\n", strerror(errno));
          goto END;
        }

        memcpy(opts_to_pass + opts_to_pass_len - 1, next_opt,
               sizeof(struct option));
        *next_id = -1;
      }
      next_dl = NULL;
    }
    //проверили все опции, готовы отправлять opts_to_pass в процесс

    void *func = dlsym(current_dl, "plugin_process_file");
    if (!func) {
      fprintf(stderr, "ERROR: dlsym() failed: %s\n", dlerror());
      goto END;
    }

    if (is_debug) {
      for (int i = 0; i < opts_to_pass_len; i++) {
        fprintf(
            stdout,
            "DEBUG: Opt_to_pass %d name: %s; has_arg: %d; val: %d(%c); flag "
            "= %s\n",
            i, (opts_to_pass + i)->name, (opts_to_pass + i)->has_arg,
            (opts_to_pass + i)->val, (opts_to_pass + i)->val,
            (char *)((opts_to_pass + i)->flag));
      }
    }

    ppf_func = (ppf_func_t)func;

    //обновляем в 0, тк новая опция
    count_ok_file = 0;

    int res = nftw(fpath, walk_func, 10, FTW_F);
    if (res < 0) {
      perror("nftw");
      goto END;
    }

    //если подходящих файлов не нашлось и аналог флага А (флаги N и О, только он
    //до этого переделался в A
    // для флага А обработка такой ситуации ниж

    if ((count_ok_file == 0) && flag_A && flag_N) {

      current_dl = NULL;
      goto NO_FILES;
    }

    if (flag_A) {

      if (!first_time) {
        for (int i = 0; i < ok_files_size; i++)
          if (ok_files[i])
            free(ok_files[i]);
        free(ok_files);
      }

      //если после новой опции файлов не прибавилось совсем
      if ((!first_time) & (!tmp)) {
        current_dl = NULL;
        goto NO_FILES;
      }
      if (tmp) {
        ok_files = (char **)malloc(tmp_size * sizeof(char *));
        if (!opts_to_pass) {
          fprintf(stderr, "ERROR: malloc() failed: %s\n", strerror(errno));
          goto END;
        }

        for (int i = 0; i < tmp_size; i++)
          *(ok_files + i) = strdup(*(tmp + i));

        for (int i = 0; i < tmp_size; i++)
          if (tmp[i])
            free(tmp[i]);

        free(tmp);

        ok_files_size = tmp_size;
      }
    }

    if (res < 0) {
      perror("nftw");
      return 1;
    }

    if (first_time)
      first_time = 0;
    //

    //значит опция уже проверена
    *current_id = -1;

    current_dl = NULL;
  }

  //вывод

  if (ok_files) {
    fprintf(stdout, "Files were found:\n");
    for (int i = 0; i < ok_files_size; i++) {
      fprintf(stdout, "\t%d\t%s\n", i, ok_files[i]);
    }
    goto END;
  }

NO_FILES:

  if (argc > 3)
    fprintf(stdout, "no files were found\n");

END:

  if (lib_holders) {
    for (int i = 0; i < longopts_size - 2; i++)

      if (lib_holders[i] != lib_holders[i + 1])
        dlclose(lib_holders[i]);

    dlclose(lib_holders[longopts_size - 2]);

    for (int i = 0; i < longopts_size - 1; i++)
      lib_holders[i] = NULL;
  }

  if (fpath)
    free(fpath);
  if (is_debug)
    fprintf(stdout, "DEBUG: fpath was freed\n");

  if (ok_files) {
    for (int i = 0; i < ok_files_size; i++)
      if (ok_files[i])
        free(ok_files[i]);
    if (ok_files)
      free(ok_files);
    if (is_debug)
      fprintf(stdout, "DEBUG: ok_files was freed\n");
  }

  if (check_is_lib_path)
    free(check_is_lib_path);
  if (is_debug)
    fprintf(stdout, "DEBUG: check_is_lib_path was freed\n");

  if (id_actual_opts) {
    free(id_actual_opts);
  }
  if (is_debug)
    fprintf(stdout, "DEBUG: id_actual_opts was freed\n");

  if (opts_to_pass)
    free(opts_to_pass);
  if (is_debug)
    fprintf(stdout, "DEBUG: opts_to_pass was freed\n");

  if (longopts) {
    for (int i = 0; i < longopts_size; i++)
      free((longopts + i)->flag);
    free(longopts);
  }
  if (is_debug)
    fprintf(stdout, "DEBUG: longopts was freed\n");

  if (lib_holders)
    free(lib_holders);
  if (is_debug)
    fprintf(stdout, "DEBUG: lib_holders was freed\n");

  return 0;
}

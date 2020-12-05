/*
 * @file: json_test.c
 * @author: Lin, Chao <chaochaox.lin@intel.com>
 * @create time: 2020-09-01 12:45:49
 * @last modified: 2020-09-01 12:45:49
 * @description:
 */
#include <stdio.h>
#include "./include/cJSON.h"

char *cjson_type[] = {"false","true","null","number","string","array","object"};

int main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("%s json-string\n");
    return 1;
  }

  cJSON *root = cJSON_Parse(argv[1]);
  if (!root) {
    printf("not a json.\n");
    return 1;
  }

#define cJSON_ChildForEach(pos, head)			for(pos = (head)->child; pos != NULL; pos = pos->child)

  printf("-------next----------\n");
  cJSON *cur;
  cJSON_ArrayForEach(cur, root) {
    printf("name=%s\n", cur->string);
    printf("type=%s\n", cjson_type[cur->type]);
    printf("string=%s\n", cur->valuestring);
    printf("int   =%d\n", cur->valueint);
    printf("double=%ld\n", cur->valuedouble);

    printf("---------------------------\n");
  }

  printf("-------child----------\n");
  cJSON_ChildForEach(cur, root) {
    printf("name=%s\n", cur->string);
    printf("type=%s\n", cjson_type[cur->type]);
    printf("string=%s\n", cur->valuestring);
    printf("int   =%d\n", cur->valueint);
    printf("double=%ld\n", cur->valuedouble);

    printf("---------------------------\n");
    if (cur->type == cJSON_Array) {
      cJSON *c;
      cJSON_ArrayForEach(c, cur) {
        printf("name=%s\n", c->string);
        printf("type=%s\n", cjson_type[c->type]);
        printf("string=%s\n", c->valuestring);
        printf("int   =%d\n", c->valueint);
        printf("double=%ld\n", c->valuedouble);

        printf("==================================\n");
      }
    }
  }


  return 0;
}


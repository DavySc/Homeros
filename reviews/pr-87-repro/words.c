#include <stdio.h>
#include <string.h>
#include "/home/claude/Homeros/src/syntax/tsql_words.h"
int main(void){ size_t nw=sizeof words/sizeof*words, nc=sizeof classes/sizeof*classes; printf("words %zu classes %zu\n",nw,nc);
 for(size_t i=1;i<nw;i++) if(strcmp(words[i-1],words[i])>=0) printf("unsorted %s %s\n",words[i-1],words[i]);
 const char*n[]={"concat_ws","datediff_big","current_timezone","current_timezone_id","json_objectagg","json_arrayagg","edit_distance","edit_distance_similarity","jaro_winkler_distance","jaro_winkler_similarity","unistr","product","regexp_like","regexp_replace","regexp_substr","regexp_instr","regexp_count","regexp_matches","regexp_split_to_table","concat"};
 for(size_t j=0;j<sizeof n/sizeof*n;j++){ int f=-1; for(size_t i=0;i<nw;i++) if(!strcmp(words[i],n[j])) f=classes[i]; printf("%s %d\n",n[j],f);} }

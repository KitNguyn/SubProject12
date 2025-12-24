#include <stdio.h>
#include <stdlib.h>

int main() {

    int *ptr1 = malloc(5 * sizeof(int));
    if (ptr1 == NULL) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    for (int i = 0; i < 5; i++) {
        ptr1[i] = i * 10;
    }
    printf("Last element in ptr1: %d\n", ptr1[4]);

    int value = 50;
    int *ptr2 = &value;  // 
    printf("Initialized value: %d\n", *ptr2);

    int *ptr3 = malloc(10 * sizeof(int));
    if (ptr3 == NULL) {
        printf("Memory allocation failed!\n");
        free(ptr1);  
        return 1;
    }
    for (int i = 0; i < 10; i++) {
        ptr3[i] = i;
    }

    free(ptr1);
    free(ptr3);

    printf("All memory freed successfully.\n");
    return 0;
}

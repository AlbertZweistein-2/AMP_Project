using value_t = int;

struct node {
    value_t data;
    struct node* next;

    node(value_t val) : data(val), next(nullptr) {};
};

struct queue {
    struct node* head;
    struct node* tail;

    queue() : head(nullptr), tail(nullptr) {};
};

void enq(value_t v, struct queue Q);

int deq(value_t *v, struct queue Q);
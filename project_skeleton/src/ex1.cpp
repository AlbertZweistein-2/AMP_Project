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

void init_queue(struct queue* Q) {
    Q->head = nullptr;
    Q->tail = nullptr;
}

void destroy_queue(struct queue* Q) {
    struct node* current = Q->head;
    struct node* next;

    while (current != nullptr) {
        next = current->next;
        delete current;
        current = next;
    }
    Q->head = nullptr;
    Q->tail = nullptr;
}

void enq(value_t v, struct queue Q);

int deq(value_t *v, struct queue Q);
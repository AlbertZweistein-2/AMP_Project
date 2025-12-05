template<typename value_t>
class Node {
private:
    value_t data;
    Node<value_t>* next;
        
public:
    Node() : data(nullptr), next(nullptr) {}; // default constructor
    Node(Node<value_t>* nxt, const value_t& val) : data(val), next(nxt) {}; // copy constructor
};

template<typename value_t>
class Queue {
    private:
        <Node<value_t>*> head;
        <Node<value_t>*> tail;


    public:
        // The Art of multiprocessor programming: Figure 10.2 constructor inspired
        Queue()
        {
            head = new Node<value_t>();
            tail = head;
        };

        ~Queue()
        {
            while(head != nullptr)
            {
                Node<value_t>* temp = head;
                head = head->next;
                delete temp;
            }
        };

        // the art of multiprocessor programming: Chapter 10.4, Figure 10.7 & 10.8
        void enq(value_t v)
        {
            Node<value_t>* newNode = new Node<value_t>(nullptr, v);
            tail->next = newNode;
            tail = newNode;
        };

        // to do: manage local free lists
        int deq(value_t* val) 
        {
            if (head->next == nullptr) {
                return 0;
            }

            val = (head->next).data;
            head = head->next;
            return 1;
        }

    
};


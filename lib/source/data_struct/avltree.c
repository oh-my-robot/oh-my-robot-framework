#include "core/data_struct/avltree.h"
#include "stdlib.h"
#include <stdio.h>
#include <string.h>

#define DEBUG_LOG 0
#define AVL_LOG_DEBUG(fmt, ...)                                                \
    do                                                                         \
    {                                                                          \
        if (DEBUG_LOG)                                                         \
        {                                                                      \
            printf("%s at %d " fmt "\r\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                                      \
    } while (0);

#define MAX(a, b) (int)((a) > (b) ? (a) : (b))

// 获取节点高度
#define HEIGHT(node)                                                                                                                \
    ((NULL == (node))                                                                                                               \
         ? 0                                                                                                                        \
         : (MAX((NULL != node->leftChild ? node->leftChild->depth : 0), (NULL != node->rightChild ? node->rightChild->depth : 0)) + \
            1))

#define ABS(a) ((a) > 0 ? (a) : (-(a)))

#define INIT_KEY (int)(-1)

typedef struct AvlNode AvlNode;
typedef struct AvlTreePrivate AvlTreePrivate;

struct AvlNode
{
    AvlNode *parent;     // 父节点
    AvlNode *leftChild;  // 左孩子
    AvlNode *rightChild; // 右孩子

    void *element; // 节点保存的元素
    int depth;     // 当前节点的高度
    int key;       // 键值
};

struct AvlTreePrivate
{
    AvlNode *root;
    uint16_t elementSize;
    uint16_t nodeCount;
};

/**
 * @brief:
 *      获取私有成员变量
 * @param:
 *      tree 树指针
 * @return:
 *      AvlTreePrivate* : 私有成员变量结构体指针
 */
static AvlTreePrivate *get_private_member(AvlTree *tree)
{
    if (NULL == tree)
        return NULL;
    return (AvlTreePrivate *)(tree->private);
}

/**
 * @brief:
 *      添加为目标节点的孩子节点
 * @param:
 *      node 要添加的节点
 *      p : 目标节点
 * @return:
 *      None.
 */
static void add_to_left(AvlNode *node, AvlNode *p)
{
    p->leftChild = node;
    node->parent = p;
    node->leftChild = node->rightChild = NULL;
    node->depth = 1;
}
static void add_to_right(AvlNode *node, AvlNode *p)
{
    p->rightChild = node;
    node->parent = p;
    node->leftChild = node->rightChild = NULL;
    node->depth = 1;
}

/**
 * @brief:
 *      LL / RR / LR / RL 类型旋转
 * @param:
 *      node : 要调整的节点
 * @return:
 *      AvlNode* 调整后的根节点（此根节点并非树的根节点，而是替换被调整那个节点的位置的节点）
 * @note:
 *      算法说明：左左旋（LL）

                    +---+							+---+
         node --->  | 5 |		         temp --->  | 3	|
                    +---+							+---+
                     / \							 / \
                    /	\							/	\
                   /	 \						   /	 \
                +---+	+---+					+---+	+---+
      temp ---> | 3 |	| 6 |		===>		| 2	|	| 5 | <--- node
                +---+	+---+					+---+	+---+
                 / \							 /  	 / \
                /	\							/	    /
               /	 \						   /	   /	 \
            +---+	+---+					+---+	+---+	+---+
            | 2 |	| 4 |					| 1 |	| 4 |	| 6 |
            +---+	+---+					+---+	+---+   +---+
             /
            /
           /
        +---+
        | 1 |
        +---+
 */
static AvlNode *ll(AvlNode *node)
{
    // AVL_LOG_DEBUG("LL %d", *((char*)(node->element) + 4));
    AvlNode *temp = node->leftChild;

    node->leftChild = temp->rightChild;
    if (NULL != temp->rightChild)
        node->leftChild->parent = node;

    temp->parent = node->parent;

    temp->rightChild = node;
    node->parent = temp;

    node->depth = HEIGHT(node); // 顺序不能错
    temp->depth = HEIGHT(temp);

    return temp;
}

static AvlNode *rr(AvlNode *node)
{
    // AVL_LOG_DEBUG("RR %d", *((char*)(node->element) + 4));
    AvlNode *temp = node->rightChild;

    node->rightChild = temp->leftChild;
    if (NULL != temp->leftChild)
        node->rightChild->parent = node;

    temp->parent = node->parent;

    temp->leftChild = node;
    node->parent = temp;

    node->depth = HEIGHT(node);
    temp->depth = HEIGHT(temp);

    return temp;
}

static AvlNode *rl(AvlNode *node)
{
    node->rightChild = ll(node->rightChild);
    return rr(node);
}

static AvlNode *lr(AvlNode *node)
{
    node->leftChild = rr(node->leftChild);
    return ll(node);
}

/**
 * @brief:
 *      调整
 * @param:
 *      node : 要调整的节点
 * @return:
 *      AvlNode* 调整后的根节点（此根节点并非树的根节点，而是替换被调整那个节点的位置的节点）
 * @note:
 *      None.
 */
static AvlNode *avl_tree_adjust(AvlNode *node)
{
    AvlNode *left_child = node->leftChild;
    AvlNode *right_child = node->rightChild;

    if (ABS(HEIGHT(left_child) - HEIGHT(right_child)) < 2)
        return node;

    if (HEIGHT(left_child) > HEIGHT(right_child))
    {
        if (NULL == left_child)
            return node;

        if (HEIGHT(left_child->leftChild) > HEIGHT(left_child->rightChild))
        {
            return ll(node);
        }
        else
        {
            return lr(node);
        }
    }
    else
    {
        if (NULL == right_child)
            return node;

        if (HEIGHT(right_child->leftChild) > HEIGHT(right_child->rightChild))
        {
            return rl(node);
        }
        else
        {
            return rr(node);
        }
    }
}

/*
 * @brief:
 *      释放节点内存
 * @param:
 *      tree : 树指针
 *      node : 要释放内存的节点
 * @return:
 *      uint8_t:    0 成功-1 失败
 */
static uint8_t avl_tree_free_node(AvlTree *tree, AvlNode *node)
{
    if (NULL == node || NULL == tree)
        return -1;

    if (NULL != tree->pfFreeElement)
        tree->pfFreeElement(node->element);

    if (NULL != node->element)
    {
        free(node->element);
        node->element = NULL;
    }

    free(node);
    node = NULL;

    return 0;
}

/*
 * @brief:
 *      创建一个节点
 * @param:
 *      tree : 树指针
 * @return:
 *      AvlNode* 创建的节点
 */
static AvlNode *avl_tree_create_node(AvlTree *tree)
{
    if (NULL == tree)
        return NULL;

    AvlTreePrivate *this = get_private_member(tree);
    AvlNode *node = (AvlNode *)malloc(sizeof(AvlNode));
    if (NULL == node)
    {
        AVL_LOG_DEBUG("[ERROR]:node malloc");
        return NULL;
    }
    node->element = malloc(this->elementSize);
    node->key = INIT_KEY;
    return node;
}

/**
 * @brief:
 *      增加节点
 * @param:
 *      tree : 树指针
 *      node : 要增加的节点
 * @return:
 *      int8_t:    -1 树指针为空；-2 创建节点失败；-3 重复插入
 */
static int8_t avl_tree_add(AvlTree *tree, void *ele)
{
    if (tree == NULL)
        return -1;

    AvlNode *node = avl_tree_create_node(tree);

    if (NULL == node)
        return -2;

    AvlTreePrivate *this = get_private_member(tree);

    AVL_LOG_DEBUG("Add key[%d]", tree->pfHash(ele));
    node->element = ele;
    // memcpy(node->element, ele, _this->elementSize);

    int key = tree->pfHash(node->element);

    if (INIT_KEY == node->key)
        node->key = key;

    if (NULL == this->root) // 添加第一个节点
    {
        node->depth = 1;
        node->leftChild = node->rightChild = node->parent = NULL;
        this->root = node;
        this->nodeCount = 1;
        return 0;
    }
    AvlNode *p = this->root;

    while (NULL != p)
    {
        if (key < p->key) // 添加到左子树
        {
            if (NULL == p->leftChild)
            {
                add_to_left(node, p);
                break;
            }
            else
            {
                p = p->leftChild;
            }
        }
        else if (key > p->key) // 添加到右子树
        {
            if (NULL == p->rightChild)
            {
                add_to_right(node, p);
                break;
            }
            else
            {
                p = p->rightChild;
            }
        }
        else
        {
            AVL_LOG_DEBUG("Element repetition");
            avl_tree_free_node(tree, node);
            return -3; // 重复
        }
    }

    while (NULL != p)
    {
        p->depth = HEIGHT(p);
        if (NULL == p->parent) // 调整到根节点
        {
            this->root = avl_tree_adjust(p);
            break;
        }
        else
        {
            if (p == p->parent->leftChild)
            {
                p = p->parent;
                p->leftChild = avl_tree_adjust(p->leftChild);
            }
            else
            {
                p = p->parent;
                p->rightChild = avl_tree_adjust(p->rightChild);
            }
        }
    }
    this->nodeCount++;
    return 0;
}

/**
 * @brief:
 *      通过键查找节
 * @param:
 *      tree : 树指针
 *      key : 节点元素对应的键
 * @return:
 *      AvlNode* 查找到的节点
 */
static AvlNode *query_by_key(AvlTree *tree, int key)
{
    if (NULL == tree)
        return NULL;
    AVL_LOG_DEBUG("Query key %d", key);
    AvlTreePrivate *this = get_private_member(tree);

    AvlNode *p = this->root;
    while (NULL != p)
    {
        if (key > p->key)
        {
            p = p->rightChild;
        }
        else if (key < p->key)
        {
            p = p->leftChild;
        }
        else
            break;
    }
    return p;
}

static void *avl_tree_query_by_key(AvlTree *tree, uint16_t key)
{
    AvlNode *node = query_by_key(tree, key);

    if (NULL == node)
        return NULL;

    return node->element;
}

/**
 * @brief:
 *      通过键删除节
 * @param:
 *      tree : 树指针
 *      key : 节点元素对应的键
 * @return:
 *      uint8_t:    0 成功  -1 失败
 */
static uint8_t avl_tree_del_by_key(AvlTree *tree, int key)
{
    if (NULL == tree)
        return -1;
    AvlTreePrivate *this = get_private_member(tree);
    AvlNode *node = query_by_key(tree, key);

    if (NULL == node)
        return -1;

    this->nodeCount--;

    AvlNode *p = node->parent; // 先保存删除节点的父节点，方便后续调整
    AvlNode *temp = NULL;      // 替换 node 节点的节点

    /*
        当该节点存在左子树或者右子树的时候，比较两边的高度；
        若左子树高度大于右子树，则取 node 节点左子树中最大的那个节点来替换 node 节点
        否则，取 node 节点右子树中最小的那个节点来替换 node 节点
    */
    if (NULL != node->leftChild || NULL != node->rightChild)
    {
        if (HEIGHT(node->leftChild) > HEIGHT(node->rightChild))
        {
            temp = node->leftChild;

            while (NULL != temp->rightChild) // 找到 node 左子树中最大的节点
            {
                temp = temp->rightChild;
            }

            if (temp != node->leftChild)
            {
                p = temp->parent;

                temp->parent->rightChild = temp->leftChild;
                if (NULL != temp->leftChild)
                    temp->leftChild->parent = temp->parent;

                temp->leftChild = node->leftChild;
                temp->leftChild->parent = temp;
            }

            temp->rightChild = node->rightChild;
            if (NULL != temp->rightChild)
                temp->rightChild->parent = temp;
        }
        else
        {
            temp = node->rightChild;
            while (NULL != temp->leftChild)
            {
                temp = temp->leftChild;
            }

            if (temp != node->rightChild)
            {
                p = temp->parent;

                temp->parent->leftChild = temp->rightChild;
                if (NULL != temp->rightChild)
                    temp->rightChild->parent = temp->parent;

                temp->rightChild = node->rightChild;
                temp->rightChild->parent = temp;
            }

            temp->parent = node->parent;

            temp->leftChild = node->leftChild;
            if (NULL != temp->leftChild)
                temp->leftChild->parent = temp;
        }

        temp->parent = node->parent;
        temp->depth = HEIGHT(temp);
    }

    if (NULL != node->parent)
    {
        if (node == node->parent->leftChild)
            node->parent->leftChild = temp;
        else if (node == node->parent->rightChild)
            node->parent->rightChild = temp;
    }
    // 如果是删除的根节点，需要更新下根节点，否则会导致根节点为NULL
    if (NULL == p)
        this->root = temp;

    while (NULL != p)
    {
        p->depth = HEIGHT(p);
        if (NULL == p->parent)
        {
            // 找到根节点
            this->root = avl_tree_adjust(p);
            break;
        }
        else
        {
            if (p == p->parent->leftChild)
            {
                p = p->parent;
                p->leftChild = avl_tree_adjust(p->leftChild);
            }
            else
            {
                p = p->parent;
                p->rightChild = avl_tree_adjust(p->rightChild);
            }
        }
    }
    avl_tree_free_node(tree, node);
    return 0;
}

/**
 * @brief:
 *      通过元素查找节点
 * @param:
 *      tree : 树指针
 *      ele : 要查找的元素
 * @return:
 *      AvlNode* 查找到的节点
 */
static AvlNode *avl_tree_query_by_element(AvlTree *tree, void *ele)
{
    if (NULL == tree || NULL == ele)
        return NULL;

    AvlTreePrivate *this = get_private_member(tree);

    uint16_t key = tree->pfHash(ele);

    AvlNode *p = this->root;
    while (NULL != p)
    {
        if (key > p->key)
        {
            p = p->rightChild;
        }
        else if (key < p->key)
        {
            p = p->leftChild;
        }
        else
            break;
    }
    return p;
}

/**
 * @brief:
 *      通过元素删除节点
 * @param:
 *      tree : 树指针
 *      ele : 节点的元素
 * @return:
 *      uint8_t:    0 成功  -1 失败
 */
static uint8_t avl_tree_del_by_element(AvlTree *tree, void *ele)
{
    AvlNode *node = avl_tree_query_by_element(tree, ele);

    if (NULL == node)
        return -1;

    return avl_tree_del_by_key(tree, node->key);
}

/**
 * @brief:
 *      前序遍历
 * @param:
 *      tree 树指针
 *      visit : 遍历时对每个元素执行的操作
 * @return:
 *      None.
 */
static void preorder(AvlNode *node, void (*visit)(void *e))
{
    if (NULL == node)
        return;

    visit(node->element);              // 访问结点
    preorder(node->leftChild, visit);  // 遍历左子树
    preorder(node->rightChild, visit); // 遍历右子树
}

static void avl_tree_preorder(AvlTree *tree, void (*visit)(void *e))
{
    AvlTreePrivate *this = get_private_member(tree);
    preorder(this->root, visit);
}

/**
 * @brief:
 *      获取树节点的数量
 * @param:
 *      tree : 树指针
 * @return:
 *      uint16_t:    树节点的数量
 */
static uint16_t avl_tree_size(AvlTree *tree)
{
    AvlTreePrivate *this = get_private_member(tree);
    return this->nodeCount;
}

/**
 * @brief:
 *      清除目标节点以及其全部子树包含的节点
 * @param:
 *      tree : 树指针
 *      node : 节点的元素
 * @return:
 *      None.
 */
static void avl_tree_node_clear(AvlTree *tree, AvlNode *node)
{
    if (NULL == node)
        return;

    avl_tree_node_clear(tree, node->leftChild);
    avl_tree_node_clear(tree, node->rightChild);

    avl_tree_free_node(tree, node);
}

/**
 * @brief:
 *      清除树的全部节点
 * @param:
 *      tree : 树指针
 * @return:
 *      None.
 */
static void avl_tree_clear(AvlTree *tree)
{
    AvlTreePrivate *this = get_private_member(tree);
    avl_tree_node_clear(tree, this->root);
    this->nodeCount = 0;
}

/**
 * @brief:
 *      销毁树
 * @param:
 *      tree : 树指针
 * @return:
 *      None.
 */
static void avl_tree_destory(AvlTree *tree)
{
    AvlTree *this = tree;

    if (NULL == this)
        return;

    if (this->getSize(this) > 0)
    {
        this->clearNode(this);
    }

    if (this->private)
    {
        free(this->private);
        this->private = NULL;
    }

    if (this)
    {
        free(this);
        this = NULL;
    }
}

/**
 * @brief:
 *      创建一棵平衡二叉树
 * @param:
 *      element_size:       节点保存元素的大小，单位字节
 *      hash_func:          从节点元素获得键key 的方法，由用户提供
 *      free_element:       若节点元素不包含额外的动态内存， 此参数可为 NULL
 *                          若节点包含的元素中还包含额外的动态内存，用户需传入此函数以正确释放内存;
 * @return:
 *      AvlTree* : 创建平衡二叉树的指针
 * @note:
 *      该树有缺陷，element_size 必须指针类型，否则会出错
 *      待完善
 */
AvlTree *avl_tree_create(int element_size, PfAvlHashFunction hash_func, PfAvlFreeElementFunction free_element)
{

    if (NULL == hash_func)
        return NULL;
    AvlTree *tree = (AvlTree *)malloc(sizeof(AvlTree));
    if (NULL == tree)
        return NULL;
    memset(tree, 0, sizeof(AvlTree));

    AvlTreePrivate *private_member = (AvlTreePrivate *)malloc(sizeof(AvlTreePrivate));
    if (NULL == private_member)
    {
        free(tree);
        return NULL;
    }

    private_member->root = NULL;
    private_member->elementSize = element_size;

    tree->private = (void *)private_member;

    tree->pfHash = hash_func;
    tree->pfFreeElement = free_element;
    tree->add = avl_tree_add;
    tree->queryByKey = avl_tree_query_by_key;
    tree->preorder = avl_tree_preorder;
    tree->getSize = avl_tree_size;
    tree->delNodeByKey = avl_tree_del_by_key;
    tree->delNodeByElement = avl_tree_del_by_element;
    tree->clearNode = avl_tree_clear;
    tree->destory = avl_tree_destory;

    return tree;
}

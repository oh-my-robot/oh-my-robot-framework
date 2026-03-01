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

// 鑾峰彇鑺傜偣楂樺害
#define HEIGHT(node)                                                                                                                \
    ((NULL == (node))                                                                                                               \
         ? 0                                                                                                                        \
         : (MAX((NULL != node->leftChild ? node->leftChild->depth : 0), (NULL != node->rightChild ? node->rightChild->depth : 0)) + \
            1))

#define ABS(a) ((a) > 0 ? (a) : (-(a)))

#define INIT_KEY (int)(-1)

typedef struct AvlNode avl_node_t;
typedef struct AvlTreePrivate avl_tree_private_t;

struct AvlNode
{
    avl_node_t *parent;     // 鐖惰妭鐐?
    avl_node_t *leftChild;  // 宸﹀瀛?
    avl_node_t *rightChild; // 鍙冲瀛?

    void *element; // 鑺傜偣淇濆瓨鐨勫厓绱?
    int depth;     // 褰撳墠鑺傜偣鐨勯珮搴?
    int key;       // 閿€?
};

struct AvlTreePrivate
{
    avl_node_t *root;
    uint16_t elementSize;
    uint16_t nodeCount;
};

/**
 * @brief:
 *      鑾峰彇绉佹湁鎴愬憳鍙橀噺
 * @param:
 *      tree 锛?鏍戞寚閽?
 * @return:
 *      avl_tree_private_t* : 绉佹湁鎴愬憳鍙橀噺缁撴瀯浣撴寚閽?
 */
static avl_tree_private_t *get_private_member(avl_tree_t *tree)
{
    if (NULL == tree)
        return NULL;
    return (avl_tree_private_t *)(tree->private);
}

/**
 * @brief:
 *      娣诲姞鍒扮洰鏍囪妭鐐圭殑 宸?鍙?瀛╁瓙
 * @param:
 *      node 锛?瑕佹坊鍔犵殑鑺傜偣
 *      p : 鐩爣鑺傜偣
 * @return:
 *      None.
 */
static void add_to_left(avl_node_t *node, avl_node_t *p)
{
    p->leftChild = node;
    node->parent = p;
    node->leftChild = node->rightChild = NULL;
    node->depth = 1;
}
static void add_to_right(avl_node_t *node, avl_node_t *p)
{
    p->rightChild = node;
    node->parent = p;
    node->leftChild = node->rightChild = NULL;
    node->depth = 1;
}

/**
 * @brief:
 *      LL / RR / LR / RL 鍨嬫棆杞?
 * @param:
 *      node : 瑕佽皟鏁寸殑鑺傜偣
 * @return:
 *      avl_node_t* 锛?璋冩暣鍚庣殑鏍硅妭鐐癸紙姝ゆ牴鑺傜偣骞堕潪鏍戠殑鏍硅妭鐐癸紝鑰屾槸鏇挎崲琚皟鏁撮偅涓妭鐐圭殑浣嶇疆鐨勮妭鐐癸級
 * @note:
 *      绠楁硶璇存槑锛氬乏宸︽棆杞?LL)

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
static avl_node_t *ll(avl_node_t *node)
{
    // AVL_LOG_DEBUG("LL %d", *((char*)(node->element) + 4));
    avl_node_t *temp = node->leftChild;

    node->leftChild = temp->rightChild;
    if (NULL != temp->rightChild)
        node->leftChild->parent = node;

    temp->parent = node->parent;

    temp->rightChild = node;
    node->parent = temp;

    node->depth = HEIGHT(node); // 椤哄簭涓嶈兘鎹?
    temp->depth = HEIGHT(temp);

    return temp;
}

static avl_node_t *rr(avl_node_t *node)
{
    // AVL_LOG_DEBUG("RR %d", *((char*)(node->element) + 4));
    avl_node_t *temp = node->rightChild;

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

static avl_node_t *rl(avl_node_t *node)
{
    node->rightChild = ll(node->rightChild);
    return rr(node);
}

static avl_node_t *lr(avl_node_t *node)
{
    node->leftChild = rr(node->leftChild);
    return ll(node);
}

/**
 * @brief:
 *      璋冩暣鏍?
 * @param:
 *      node : 瑕佽皟鏁寸殑鑺傜偣
 * @return:
 *      avl_node_t* 锛?璋冩暣鍚庣殑鏍硅妭鐐癸紙姝ゆ牴鑺傜偣骞堕潪鏍戠殑鏍硅妭鐐癸紝鑰屾槸鏇挎崲琚皟鏁撮偅涓妭鐐圭殑浣嶇疆鐨勮妭鐐癸級
 * @note:
 *      None.
 */
static avl_node_t *avl_tree_adjust(avl_node_t *node)
{
    avl_node_t *left_child = node->leftChild;
    avl_node_t *right_child = node->rightChild;

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
 *      閲婃斁鑺傜偣鍐呭瓨
 * @param:
 *      tree : 鏍戞寚閽?
 *      node : 瑕侀噴鏀惧唴瀛樼殑鑺傜偣
 * @return:
 *      uint8_t:    0 鎴愬姛锛?-1 澶辫触
 */
static uint8_t avl_tree_free_node(avl_tree_t *tree, avl_node_t *node)
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
 *      鍒涘缓涓€涓妭鐐?
 * @param:
 *      tree : 鏍戞寚閽?
 * @return:
 *      avl_node_t* 锛?鍒涘缓鐨勮妭鐐?
 */
static avl_node_t *avl_tree_create_node(avl_tree_t *tree)
{
    if (NULL == tree)
        return NULL;

    avl_tree_private_t *this = get_private_member(tree);
    avl_node_t *node = (avl_node_t *)malloc(sizeof(avl_node_t));
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
 *      澧炲姞鑺傜偣
 * @param:
 *      tree : 鏍戞寚閽?
 *      node : 瑕佸鍔犵殑鑺傜偣
 * @return:
 *      int8_t:    -1 鏍戞寚閽堜负绌猴紝 -2 鍒涘缓鑺傜偣澶辫触锛?-3 閲嶅鎻掑叆
 */
static int8_t avl_tree_add(avl_tree_t *tree, void *ele)
{
    if (tree == NULL)
        return -1;

    avl_node_t *node = avl_tree_create_node(tree);

    if (NULL == node)
        return -2;

    avl_tree_private_t *this = get_private_member(tree);

    AVL_LOG_DEBUG("Add key[%d]", tree->pfHash(ele));
    node->element = ele;
    // memcpy(node->element, ele, _this->elementSize);

    int key = tree->pfHash(node->element);

    if (INIT_KEY == node->key)
        node->key = key;

    if (NULL == this->root) // 娣诲姞绗竴涓妭鐐?
    {
        node->depth = 1;
        node->leftChild = node->rightChild = node->parent = NULL;
        this->root = node;
        this->nodeCount = 1;
        return 0;
    }
    avl_node_t *p = this->root;

    while (NULL != p)
    {
        if (key < p->key) // 娣诲姞鍒板乏瀛愭爲
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
        else if (key > p->key) // 娣诲姞鍒板彸瀛愭爲
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
            return -3; // 閲嶅
        }
    }

    while (NULL != p)
    {
        p->depth = HEIGHT(p);
        if (NULL == p->parent) // 璋冩暣鍒版牴鑺傜偣
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
 *      閫氳繃閿€兼煡鎵捐妭鐐?
 * @param:
 *      tree : 鏍戞寚閽?
 *      key : 鑺傜偣鍏冪礌瀵瑰簲鐨勯敭鍊?
 * @return:
 *      avl_node_t* 锛?鏌ユ壘鍒扮殑鑺傜偣
 */
static avl_node_t *query_by_key(avl_tree_t *tree, int key)
{
    if (NULL == tree)
        return NULL;
    AVL_LOG_DEBUG("Query key %d", key);
    avl_tree_private_t *this = get_private_member(tree);

    avl_node_t *p = this->root;
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

static void *avl_tree_query_by_key(avl_tree_t *tree, uint16_t key)
{
    avl_node_t *node = query_by_key(tree, key);

    if (NULL == node)
        return NULL;

    return node->element;
}

/**
 * @brief:
 *      閫氳繃閿€煎垹闄よ妭鐐?
 * @param:
 *      tree : 鏍戞寚閽?
 *      key : 鑺傜偣鍏冪礌瀵瑰簲鐨勯敭鍊?
 * @return:
 *      uint8_t:    0 鎴愬姛  -1 澶辫触
 */
static uint8_t avl_tree_del_by_key(avl_tree_t *tree, int key)
{
    if (NULL == tree)
        return -1;
    avl_tree_private_t *this = get_private_member(tree);
    avl_node_t *node = query_by_key(tree, key);

    if (NULL == node)
        return -1;

    this->nodeCount--;

    avl_node_t *p = node->parent; // 鍏堜繚瀛樺垹闄よ妭鐐圭殑鐖惰妭鐐癸紝鏂逛究鍚庨潰璋冩暣鏍?
    avl_node_t *temp = NULL;      // 鏇挎崲 node 鑺傜偣鐨勮妭鐐?

    /*
        褰撹鑺傜偣瀛樺湪宸﹀瓙鏍戞垨鑰呭彸瀛愭爲鐨勬椂鍊欙紝姣旇緝涓よ竟鐨勯珮搴︼紱
        鑻ュ乏瀛愭爲楂樺害澶т簬鍙冲瓙鏍戯紝鍒欏彇 node 鑺傜偣宸﹀瓙鏍戜腑鏈€澶х殑閭ｄ釜鑺傜偣鏉ユ浛鎹?node 鑺傜偣
        鍚﹀垯锛屽彇 node 鑺傜偣鍙冲瓙鏍戜腑鏈€灏忕殑閭ｄ釜鑺傜偣鏉ユ浛鎹?node 鑺傜偣
    */
    if (NULL != node->leftChild || NULL != node->rightChild)
    {
        if (HEIGHT(node->leftChild) > HEIGHT(node->rightChild))
        {
            temp = node->leftChild;

            while (NULL != temp->rightChild) // 鎵惧埌 node 宸﹀瓙鏍戜腑鏈€澶х殑鑺傜偣
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
    // 濡傛灉鏄垹闄ょ殑鏍硅妭鐐癸紝闇€瑕佹洿鏂颁笅鏍硅妭鐐癸紝鍚﹀垯浼氬鑷存牴鑺傜偣涓篘ULL
    if (NULL == p)
        this->root = temp;

    while (NULL != p)
    {
        p->depth = HEIGHT(p);
        if (NULL == p->parent)
        {
            // 鎵惧埌鏍硅妭鐐?
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
 *      閫氳繃鍏冪礌鏌ユ壘鑺傜偣
 * @param:
 *      tree : 鏍戞寚閽?
 *      ele : 瑕佹煡鎵剧殑鍏冪礌
 * @return:
 *      avl_node_t* 锛?鏌ユ壘鍒扮殑鑺傜偣
 */
static avl_node_t *avl_tree_query_by_element(avl_tree_t *tree, void *ele)
{
    if (NULL == tree || NULL == ele)
        return NULL;

    avl_tree_private_t *this = get_private_member(tree);

    uint16_t key = tree->pfHash(ele);

    avl_node_t *p = this->root;
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
 * @biref:
 *      閫氳繃鍏冪礌鍒犻櫎鑺傜偣
 * @param:
 *      tree : 鏍戞寚閽?
 *      ele : 鑺傜偣鐨勫厓绱?
 * @return:
 *      uint8_t:    0 鎴愬姛  -1 澶辫触
 */
static uint8_t avl_tree_del_by_element(avl_tree_t *tree, void *ele)
{
    avl_node_t *node = avl_tree_query_by_element(tree, ele);

    if (NULL == node)
        return -1;

    return avl_tree_del_by_key(tree, node->key);
}

/**
 * @brief:
 *      鍓嶅簭閬嶅巻
 * @param:
 *      tree 锛?鏍戞寚閽?
 *      visit : 閬嶅巻鏃跺姣忎釜鍏冪礌鎵ц鐨勬搷浣?
 * @return:
 *      None.
 */
static void preorder(avl_node_t *node, void (*visit)(void *e))
{
    if (NULL == node)
        return;

    visit(node->element);              // 璁块棶缁撶偣
    preorder(node->leftChild, visit);  // 閬嶅巻宸﹀瓙鏍?
    preorder(node->rightChild, visit); // 閬嶅巻鍙冲瓙鏍?
}

static void avl_tree_preorder(avl_tree_t *tree, void (*visit)(void *e))
{
    avl_tree_private_t *this = get_private_member(tree);
    preorder(this->root, visit);
}

/**
 * @brief:
 *      鑾峰彇鏍戣妭鐐圭殑鏁伴噺
 * @param:
 *      tree : 鏍戞寚閽?
 * @return:
 *      uint16_t:    鏍戣妭鐐圭殑鏁伴噺
 */
static uint16_t avl_tree_size(avl_tree_t *tree)
{
    avl_tree_private_t *this = get_private_member(tree);
    return this->nodeCount;
}

/**
 * @brief:
 *      娓呴櫎鐩爣鑺傜偣浠ュ強鍏跺叏閮ㄥ瓙鏍戝寘鍚殑鑺傜偣
 * @param:
 *      tree : 鏍戞寚閽?
 *      node : 鑺傜偣鐨勫厓绱?
 * @return:
 *      None.
 */
static void avl_tree_node_clear(avl_tree_t *tree, avl_node_t *node)
{
    if (NULL == node)
        return;

    avl_tree_node_clear(tree, node->leftChild);
    avl_tree_node_clear(tree, node->rightChild);

    avl_tree_free_node(tree, node);
}

/**
 * @brief:
 *      娓呴櫎鏍戠殑鍏ㄩ儴鑺傜偣
 * @param:
 *      tree : 鏍戞寚閽?
 * @return:
 *      None.
 */
static void avl_tree_clear(avl_tree_t *tree)
{
    avl_tree_private_t *this = get_private_member(tree);
    avl_tree_node_clear(tree, this->root);
    this->nodeCount = 0;
}

/**
 * @brief:
 *      閿€姣佹爲
 * @param:
 *      tree : 鏍戞寚閽?
 * @return:
 *      None.
 */
static void avl_tree_destory(avl_tree_t *tree)
{
    avl_tree_t *this = tree;

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
 *      鍒涘缓涓€棰楀钩琛′簩鍙夋爲
 * @param:
 *      element_size:       鑺傜偣淇濆瓨鍏冪礌鐨勫ぇ灏忥紝鍗曚綅瀛楄妭
 *      hash_func:          浠庤妭鐐瑰厓绱犺幏寰楅敭鍊?key 鐨勬柟娉曪紝鐢辩敤鎴锋彁渚?
 *      free_element:       鑻ヨ妭鐐瑰厓绱犱笉鍖呭惈棰濆鐨勫姩鎬佸唴瀛橈紝 姝ゅ弬鏁板彲浼?NULL锛?
 *                          鑻ヨ妭鐐瑰寘鍚殑鍏冪礌涓繕鍖呭惈棰濆鐨勫姩鎬佸唴瀛橈紝鐢ㄦ埛闇€浼犲叆姝ゅ嚱鏁颁互姝ｇ‘閲婃斁鍐呭瓨;
 * @return:
 *      avl_tree_t* : 鍒涘缓骞宠　浜屽弶鏍戠殑鎸囬拡
 * @note:
 *      璇ユ爲鏈夌己闄凤紝element_size 蹇呴』鏄?鎸囬拡绫诲瀷锛屽惁鍒欎細鍑洪敊
 *      寰呭畬鍠?
 */
avl_tree_t *avl_tree_create(int element_size, pf_avl_hash hash_func, pf_avl_free_element free_element)
{

    if (NULL == hash_func)
        return NULL;
    avl_tree_t *tree = (avl_tree_t *)malloc(sizeof(avl_tree_t));
    if (NULL == tree)
        return NULL;
    memset(tree, 0, sizeof(avl_tree_t));

    avl_tree_private_t *private_member = (avl_tree_private_t *)malloc(sizeof(avl_tree_private_t));
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

#ifndef __MY_AVL_TREE__H
#define __MY_AVL_TREE__H

#include <stdint.h>

typedef struct AvlTree AvlTree;
typedef void (*AvlVisitFunction)(void* ele);           // 用户提供的元素访问操作
typedef void (*PfAvlFreeElementFunction)(void* ele); // 用户提供的节点元素中保存的动态内存的释放方法
typedef uint16_t (*PfAvlHashFunction)(void* ele);     // 用户提供的 hash 函数

/*
 * @brief:
 *      添加节点数据
 * @param:
 *		avl_root:	根节点
 *		data:		指向数据的指针
 * @return:
 *		int8_t:		-1 树指针为空， -2 创建节点失败， -3 重复插入
 */
typedef int8_t (*PfAvlAddFunction)(AvlTree* avl_root, void* data);

/*
 * @brief:
 *      通过键值查找节点
 * @param:
 *		avl_root:	根节点
 *		key:		键值
 * @return:
 *		(void*):	数据的指针
 */
typedef void* (*PfAvlQueryByKeyFunction)(AvlTree* avl_root, uint16_t key);

/*
 * @brief:
 *      前序遍历
 * @param:
 *		avl_root:	根节点
 *		visit:		遍历时对每个元素执行的操作
 * @return:
 *		none.
 */
typedef void (*PfAvlPreorderFunction)(AvlTree* avl_root, AvlVisitFunction visit);

/*
 * @brief:
 *      获取节点数
 * @param:
 *		avl_root:	根节点
 */
typedef uint16_t (*PfAvlGetSizeFunction)(AvlTree* avl_root);

/*
 * @brief:
 *      通过键值删除节点
 * @param:
 *      avl_root:   根节点
 *      key:        键值
 * @return:
 *      uint8_t:    0 成功  -1 失败
 */
typedef uint8_t (*PfAvlDelNodeByKeyFunction)(AvlTree* avl_root, int key);

/*
 * @brief:
 *      通过元素删除节点
 * @param:
 *      avl_root:   树指针
 *      element:    节点的元素
 * @return:
 *      uint8_t:    0 成功  -1 失败
 */
typedef uint8_t (*PfAvlDelNodeByElementFunction)(AvlTree* avl_root, void* element);

/*
 * @brief:
 *		清除树的全部节点
 * @param:
 *		avl_root : 根节点
 * @return:
 *		none.
 */
typedef void (*PfAvlClearNodeFunction)(AvlTree* avl_root);

/*
 * @brief:
 *      销毁树
 * @param:
 *      avl_root:   根节点
 * @return:
 *      none.
 */
typedef void (*PfAvlDestoryFunction)(AvlTree* avl_root);

struct AvlTree
{
    void* private;
    PfAvlFreeElementFunction pfFreeElement;
    PfAvlHashFunction pfHash;
    PfAvlAddFunction add;
    PfAvlQueryByKeyFunction queryByKey;
    PfAvlPreorderFunction preorder;
    PfAvlGetSizeFunction getSize;
    PfAvlDelNodeByKeyFunction delNodeByKey;
    PfAvlDelNodeByElementFunction delNodeByElement;
    PfAvlClearNodeFunction clearNode;
    PfAvlDestoryFunction destory;
};

AvlTree* avl_tree_create(int element_size, PfAvlHashFunction hash_func, PfAvlFreeElementFunction free_element);

#endif

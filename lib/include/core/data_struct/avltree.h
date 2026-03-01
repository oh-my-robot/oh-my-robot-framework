#ifndef __MY_AVL_TREE__H
#define __MY_AVL_TREE__H

#include <stdint.h>

typedef struct AvlTree avl_tree_t;
typedef void (*avl_visit)(void* ele);           // 用户提供的元素访问操作
typedef void (*pf_avl_free_element)(void* ele); // 用户提供的节点元素中保存的动态内存的释放方法
typedef uint16_t (*pf_avl_hash)(void* ele);     // 用户提供的 hash 函数

/*
 * @brief:
 *      添加节点数据
 * @param:
 *		avl_root:	根节点
 *		data:		指向数据的指针
 * @return:
 *		int8_t:		-1 树指针为空， -2 创建节点失败， -3 重复插入
 */
typedef int8_t (*pf_avl_add)(avl_tree_t* avl_root, void* data);

/*
 * @brief:
 *      通过键值查找节点
 * @param:
 *		avl_root:	根节点
 *		key:		键值
 * @return:
 *		(void*):	数据的指针
 */
typedef void* (*pf_avl_query_by_key)(avl_tree_t* avl_root, uint16_t key);

/*
 * @brief:
 *      前序遍历
 * @param:
 *		avl_root:	根节点
 *		visit:		遍历时对每个元素执行的操作
 * @return:
 *		none.
 */
typedef void (*pf_avl_preorder)(avl_tree_t* avl_root, avl_visit visit);

/*
 * @brief:
 *      获取节点数
 * @param:
 *		avl_root:	根节点
 */
typedef uint16_t (*pf_avl_get_size)(avl_tree_t* avl_root);

/*
 * @brief:
 *      通过键值删除节点
 * @param:
 *      avl_root:   根节点
 *      key:        键值
 * @return:
 *      uint8_t:    0 成功  -1 失败
 */
typedef uint8_t (*pf_avl_del_node_by_key)(avl_tree_t* avl_root, int key);

/*
 * @brief:
 *      通过元素删除节点
 * @param:
 *      avl_root:   树指针
 *      element:    节点的元素
 * @return:
 *      uint8_t:    0 成功  -1 失败
 */
typedef uint8_t (*pf_avl_del_node_by_element)(avl_tree_t* avl_root, void* element);

/*
 * @brief:
 *		清除树的全部节点
 * @param:
 *		avl_root : 根节点
 * @return:
 *		none.
 */
typedef void (*pf_avl_clear_node)(avl_tree_t* avl_root);

/*
 * @brief:
 *      销毁树
 * @param:
 *      avl_root:   根节点
 * @return:
 *      none.
 */
typedef void (*pf_avl_destory)(avl_tree_t* avl_root);

struct AvlTree
{
    void* private;
    pf_avl_free_element pfFreeElement;
    pf_avl_hash pfHash;
    pf_avl_add add;
    pf_avl_query_by_key queryByKey;
    pf_avl_preorder preorder;
    pf_avl_get_size getSize;
    pf_avl_del_node_by_key delNodeByKey;
    pf_avl_del_node_by_element delNodeByElement;
    pf_avl_clear_node clearNode;
    pf_avl_destory destory;
};

avl_tree_t* avl_tree_create(int element_size, pf_avl_hash hash_func, pf_avl_free_element free_element);

#endif

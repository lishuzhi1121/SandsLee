#include <stdio.h>
#include <stdlib.h>

// BST(Binary Search Tree)二叉搜索树：满足父节点必须大于左孩子小于右孩子。

typedef struct node {
	int data;
	struct node *left;
	struct node *right;
} TreeNode;

typedef struct tree {
	TreeNode *root;
} Tree;

// 求一棵树中的最大节点
int get_max(TreeNode *root) {
	if (root == NULL) return -1;
	// 递归左子树的最大值
	int ml = get_max(root->left);
	// 递归右子树的最大值
	int mr = get_max(root->right);
	// 根节点
	int m = root->data;
	int max = m;
	if (ml > max) max = ml;
	if (mr > max) max = mr;
	return max;
}

// 求一棵树的高度
int get_treeheight(TreeNode *root) {
	if (root == NULL) return 0;
	int left_h = get_treeheight(root->left);
	int right_h = get_treeheight(root->right);
	return (left_h > right_h ? left_h : right_h) + 1;
}


// 先序遍历（先、中、后指的都是根节点什么时候访问）
void preorder(TreeNode *root) {
        if (root != NULL)
        {
                printf("%d\n", root->data);
                preorder(root->left);
                preorder(root->right);
        }
}

// 中序遍历
void inorder(TreeNode *root) {
        if (root != NULL)
        {
                inorder(root->left);
                printf("%d\n", root->data);
                inorder(root->right);
        }
}

// 后序遍历
void postorder(TreeNode *root) {
        if (root != NULL) {
                postorder(root->left);
                postorder(root->right);
                printf("%d\n", root->data);
        }
}


// 往BST(Binary Search Tree)树中插入元素
void insert(Tree *tree, int value) {
	// 将value包装成Node
	TreeNode *node  = malloc(sizeof(TreeNode));
	node->data  = value;
	node->left  = NULL;
	node->right = NULL;

	if (tree->root == NULL) {
		tree->root = node;
	} else {
		TreeNode *tmp = tree->root;
		while(tmp != NULL) {
			if (value < tmp->data) {
				if (tmp->left == NULL) {
					tmp->left = node;
					return;
				}
				tmp = tmp->left;
			} else {
				if (tmp->right == NULL) {
					tmp->right = node;
					return;
				}
				tmp = tmp->right;
			}
		}
	}
}





int main(int argc, char const *argv[]) {
	int arr[] = {2,8,9,3,4,6,1,5,7};
	int len = sizeof(arr) / sizeof(arr[0]);
	Tree tree;
	tree.root = NULL;
	for (int i=0; i<len; i++) {
		insert(&tree, arr[i]);
	}
	
	printf("--- preorder ---\n");
	preorder(tree.root);
	
	printf("--- inorder ---\n");
	inorder(tree.root);
	
	printf("Tree height: %d \n", get_treeheight(tree.root));
	printf("Tree max node data: %d \n", get_max(tree.root));
}


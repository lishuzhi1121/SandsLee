#include <stdio.h>
#include <stdlib.h>

typedef struct node {
	int data;
	struct node *left;
	struct node *right;
} TreeNode;


typedef struct tree {
	TreeNode *root;
} Tree;


void createTree(int *arr, int size, int index, TreeNode **node) {
	if (size < 1)
	{
		*node = NULL;
		return;
	}

	*node = (TreeNode *)malloc(sizeof(TreeNode));
	(*node)->data = *(arr + index);
	(*node)->left = NULL;
	(*node)->right = NULL;

	createTree(arr, size, 2*index + 1, &((*node)->left));
	createTree(arr, size, 2*index + 2, &((*node)->right));
}

// 插入元素
void insert(TreeNode *root, int value) {
	TreeNode *node = malloc(sizeof(TreeNode));
	node->data = value;
	node->left = NULL;
	node->right = NULL;
	if (root == NULL) root = node;
	TreeNode *tmp = root;
	while(tmp != NULL) {
		if (tmp->left == NULL) {
			tmp->left = node;
			return;
		}
		if (tmp->right == NULL) {
			tmp->right = node;
			return;
		}
	}
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

int main(int argc, char const *argv[])
{
	// TreeNode n0;
	// TreeNode n1;
	// TreeNode n2;
	// TreeNode n3;
	// TreeNode n4;

	// n0.data = 5;
	// n1.data = 7;
	// n2.data = 8;
	// n3.data = 1;
	// n4.data = 2;

	// n0.left = &n1;
	// n0.right = &n2;
	// n1.left = &n3;
	// n1.right = NULL;
	// n2.left = NULL;
	// n2.right = &n4;
	// n3.left = NULL;
	// n3.right = NULL;
	// n4.left = NULL;
	// n4.right = NULL;

	int arr[] = {5,7,8,1,2};

	// Tree *tree;
	// tree.root = NULL;

	TreeNode *root;
	root = NULL;
	createTree(arr, 5, 0, &root);
	
	printf("--- preorder ---\n");
	preorder(root);

	// printf("--- inorder ---\n");
	// inorder(&n0);

	// printf("--- postorder ---\n");
	// postorder(&n0);
	return 0;
}

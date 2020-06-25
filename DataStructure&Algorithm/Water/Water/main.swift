//
//  main.swift
//  Water
//
//  Created by SandsLee on 2020/6/22.
//  Copyright © 2020 SandsLee. All rights reserved.
//

import Foundation

// 定义数组中每一个元素的结构 (xi, xj, h), 对应到坐标轴上描述的矩形暂且称之为"墙"
struct Wall {
    var xi = 0
    var xj = 0
    var h = 0
    
    init(xi x1: Int, xj x2: Int, h y: Int) {
        self.xi = x1
        self.xj = x2
        self.h = y
    }
}


let arr = [
    (1, 2, 2),
    (3, 4, 3),
    (5, 8, 1),
    (9, 10, 2),
]

let res = trapWater(walls: arr)
print(res)


/// 计算给定二维数组在坐标轴上的描述可以盛多少滴水
/// - Parameter w: 二维数组
/// - Returns: 可以盛的水滴数
func trapWater(walls w: [(Int, Int, Int)]) -> Int {
    // 转换为高度数组
    let heights = calHeights(walls: w.map{ Wall(xi: $0.0, xj: $0.1, h: $0.2) })
//    print(heights)
    
    var l = 0
    var r = heights.count - 1
    var maxl = 0
    var maxr = 0
    var ans = 0
    
    while l < r {
        // 如果左指针的值小于右指针的值, 则左指针前进, 否则右指针后退
        if heights[l] < heights[r] {
            // 如果左指针的值大于记录的左指针最大值, 则更新记录的最大值, 否则说明出现了低洼可以盛水, 直接累计差值
            if heights[l] > maxl {
                maxl = heights[l]
            } else {
                ans += maxl - heights[l]
            }
            
            l += 1
        } else {
            // 右边同理
            if heights[r] > maxr {
                maxr = heights[r]
            } else {
                ans += maxr - heights[r]
            }
            
            r -= 1
        }
    }
    
    return ans
}


/// 计算给定的二位数组在坐标轴上每个位置的高度, 例如给定数组 [(1,2,2),(3,4,3),(5,8,1),(9,10,2)], 描述到平面坐标轴上转换为高度数组为 [0, 2, 0, 3, 0, 1, 1, 1, 0, 2]
/// - Parameter w: 二维数组
/// - Returns: 平面高度数组
func calHeights(walls w:[Wall]) -> [Int] {
    var heights: [Int] = []
    var start = 0 // 起始位置
    // 遍历二维数组
    for i in 0..<w.count {
        let wall = w[i]
        // 如果 xi 大于前一个元素的 xj（起始xj不存在,所以设为0）,说明前一个元素和后一个元素之间有间隙, 需要补0
        var tmp = wall.xi - start
        while tmp > 0 {
            heights.append(0)
            tmp -= 1
        }
        // 如果当前元素的 xj 大于 xi, 则说明当前元素构成墙体, 高度为 h, 所以直接填充 h 即可
        tmp = wall.xj - wall.xi
        while tmp > 0 {
            heights.append(wall.h)
            tmp -= 1
        }
        // 更新起始位置
        start = wall.xj
    }
    
    return heights
}






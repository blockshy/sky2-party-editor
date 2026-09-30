// 功能区方向导航的纯几何策略。不依赖 ImGui、窗口、输入设备或帧状态；
// 调用者只提交本帧可导航控件的矩形，随后自行把返回索引映射为控件 ID。
#pragma once
#include <cstddef>
#include <cmath>
#include <vector>

namespace sky2solo::navigation {
struct Rect {float x1,y1,x2,y2;};
enum class Direction {None,Up,Down,Left,Right};

namespace detail {
inline bool Valid(const Rect& rect){
    return std::isfinite(rect.x1)&&std::isfinite(rect.y1)&&std::isfinite(rect.x2)&&std::isfinite(rect.y2)&&
        rect.x2>rect.x1&&rect.y2>rect.y1;
}
inline double CenterX(const Rect& rect){return (double(rect.x1)+double(rect.x2))*.5;}
inline double CenterY(const Rect& rect){return (double(rect.y1)+double(rect.y2))*.5;}
}

// 返回目标在 items 中的索引；没有邻居、当前索引无效或方向为 None 时返回 -1。
// anchorX 是纵向移动时保留的列锚点，而不是每到宽控件就重置为它的中心。
// 上下只允许覆盖该锚点的控件，边界不绕回；左右不使用锚点，按实际矩形
// 相邻关系寻找另一列。所有完全相同的排序条件以较小索引稳定决胜。
inline int Neighbor(const std::vector<Rect>& items,int current,Direction direction,float anchorX,
                    const std::vector<Rect>* lanes=nullptr){
    if(current<0||static_cast<std::size_t>(current)>=items.size()||direction==Direction::None)return -1;
    const auto& source=items[static_cast<std::size_t>(current)];if(!detail::Valid(source))return -1;
    const bool vertical=direction==Direction::Up||direction==Direction::Down;
    const bool horizontal=direction==Direction::Left||direction==Direction::Right;
    if(!vertical&&!horizontal)return -1;
    const double sourceY=detail::CenterY(source);
    const double anchor=std::isfinite(anchorX)?double(anchorX):detail::CenterX(source);
    int best=-1;bool bestOverlap=false;double bestPrimary=0,bestSecondary=0;
    for(std::size_t index=0;index<items.size();++index){
        if(index==static_cast<std::size_t>(current))continue;
        const auto& candidate=items[index];if(!detail::Valid(candidate))continue;
        const double candidateY=detail::CenterY(candidate);bool overlaps=false;
        double primary=0,secondary=0;
        if(vertical){
            // 列矩形来自实际卡片/表格，而非短按钮本身的宽度。禁用“上一页”
            // 后，同一行仍有可用“下一页”，必须先进入这一行，而不是跳过。
            // 无布局信息时保持严格矩形策略，调用者不会隐式跨入另一栏。
            const auto& lane=lanes&&lanes->size()==items.size()?(*lanes)[index]:candidate;
            if(anchor<lane.x1||anchor>lane.x2)continue;
            if((direction==Direction::Up&&candidate.y2>source.y1+2.0f)||
               (direction==Direction::Down&&candidate.y1<source.y2-2.0f))continue;
            primary=std::abs(candidateY-sourceY);
            // 同排先选择覆盖原锚点的控件，再选距离锚点最近的可用控件。
            // 不将同排中高度不同的按钮误判为上/下邻居。
            secondary=anchor<candidate.x1?candidate.x1-anchor:anchor>candidate.x2?anchor-candidate.x2:0;
            secondary+=std::abs(detail::CenterX(candidate)-anchor)*.0001;
        }else{
            // 允许边框/像素舍入产生最多 2 像素重叠，但不把覆盖当前控件
            // 大片区域的宽行误认为左右相邻列。
            if(direction==Direction::Right){
                if(candidate.x1<source.x2-2.0f)continue;
                primary=double(candidate.x1)-source.x2;
            }else{
                if(candidate.x2>source.x1+2.0f)continue;
                primary=double(source.x1)-candidate.x2;
            }
            if(primary<0)primary=0;
            overlaps=candidate.y1<source.y2&&candidate.y2>source.y1;
            secondary=std::abs(candidateY-sourceY);
        }
        // 水平先同排，再相邻列 X 间隙，最后选该列最近的 Y；无同排候选时
        // 仍可进入相邻列最近的控件。非整数 DPI 下，同一窗口内的按钮可能
        // 一项保持 927.815px、另一项已取整为 927px：不能把这不到一像素
        // 的差当作“更近的一列”，从而选择纵向更远的按钮。与侧向重叠判断
        // 一样，仅对 2px 内的间隙差按同列处理，仍保留不同列的距离优先级。
        const bool sameHorizontalGap=horizontal&&std::abs(primary-bestPrimary)<=2.0;
        const bool closer=sameHorizontalGap?secondary<bestSecondary:
            primary<bestPrimary||(primary==bestPrimary&&secondary<bestSecondary);
        const bool better=best<0||(horizontal&&overlaps&&!bestOverlap)||
            ((!horizontal||overlaps==bestOverlap)&&closer);
        if(better){best=static_cast<int>(index);bestOverlap=overlaps;bestPrimary=primary;bestSecondary=secondary;}
    }
    return best;
}
}

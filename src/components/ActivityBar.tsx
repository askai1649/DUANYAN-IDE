// Activity Bar — VSCode 式竖直图标栏 (Codicon 官方图标集, MIT)
// 一期重构: 取代原侧栏横排 tabs (FILES/WS/GIT/LINT)

export interface ActivityItem {
  id: string;
  icon: string; // codicon 类名后缀, 如 "files" -> codicon-files
  title: string;
}

interface Props {
  items: ActivityItem[];
  active: string | null; // null = 侧栏隐藏
  onSelect: (id: string) => void;
}

export default function ActivityBar({ items, active, onSelect }: Props) {
  return (
    <div className="activity-bar">
      {items.map(it => (
        <button
          key={it.id}
          className={"activity-item" + (active === it.id ? " active" : "")}
          title={it.title}
          onClick={() => onSelect(it.id)}
        >
          <i className={`codicon codicon-${it.icon}`} />
        </button>
      ))}
    </div>
  );
}

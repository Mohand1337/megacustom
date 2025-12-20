import React, { useState } from 'react';
import { 
  Cloud, FolderSync, Upload, RefreshCw, Settings, ArrowLeft, ArrowRight, 
  ArrowUp, Search, FolderPlus, Trash2, List, Grid, LayoutGrid, 
  ChevronRight, Folder, File, FileText, Image, Archive, Play, Pause, 
  X, Check, AlertCircle, Download, ChevronDown, Eye, Edit, Plus, Minus,
  HardDrive, Clock, Zap
} from 'lucide-react';

// Color constants from spec
const colors = {
  megaRed: '#D90007',
  redHover: '#C00006',
  redLight: '#FFE6E7',
  white: '#FFFFFF',
  lightGray: '#FAFAFA',
  mediumGray: '#E0E0E0',
  textPrimary: '#333333',
  textSecondary: '#666666',
  disabled: '#AAAAAA',
  progressBg: '#E8E8E8',
};

// ============ REUSABLE COMPONENTS ============

const Button = ({ children, primary, icon, disabled, onClick, className = '' }) => (
  <button
    onClick={onClick}
    disabled={disabled}
    className={`
      inline-flex items-center gap-2 px-4 py-2 rounded-md text-sm font-medium transition-all
      ${primary 
        ? 'bg-[#D90007] text-white hover:bg-[#C00006] active:bg-[#A00005]' 
        : 'bg-white text-[#333] border border-[#E0E0E0] hover:bg-[#F5F5F5] active:bg-[#EBEBEB]'
      }
      ${disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'}
      ${className}
    `}
  >
    {icon && <span className="w-4 h-4">{icon}</span>}
    {children}
  </button>
);

const IconButton = ({ icon, onClick, active, title }) => (
  <button
    onClick={onClick}
    title={title}
    className={`
      w-9 h-9 flex items-center justify-center rounded-md transition-all
      ${active 
        ? 'bg-[#FFE6E7] text-[#D90007]' 
        : 'text-[#555] hover:bg-[#F0F0F0]'
      }
    `}
  >
    {icon}
  </button>
);

const Input = ({ placeholder, value, onChange, icon, className = '' }) => (
  <div className={`relative ${className}`}>
    {icon && <span className="absolute left-3 top-1/2 -translate-y-1/2 text-[#999]">{icon}</span>}
    <input
      type="text"
      placeholder={placeholder}
      value={value}
      onChange={onChange}
      className={`
        w-full h-9 px-3 ${icon ? 'pl-10' : ''} rounded-md border border-[#E0E0E0] 
        bg-white text-sm focus:outline-none focus:border-[#D90007] focus:ring-2 focus:ring-[#D90007]/20
      `}
    />
  </div>
);

const Checkbox = ({ checked, onChange, label }) => (
  <label className="flex items-center gap-2 cursor-pointer select-none">
    <div 
      onClick={() => onChange(!checked)}
      className={`
        w-[18px] h-[18px] rounded flex items-center justify-center border-2 transition-all
        ${checked 
          ? 'bg-[#D90007] border-[#D90007]' 
          : 'bg-white border-[#CCC] hover:border-[#999]'
        }
      `}
    >
      {checked && <Check size={12} className="text-white" strokeWidth={3} />}
    </div>
    <span className="text-sm text-[#333]">{label}</span>
  </label>
);

const ProgressBar = ({ percent, className = '' }) => (
  <div className={`h-2 bg-[#E8E8E8] rounded-full overflow-hidden ${className}`}>
    <div 
      className="h-full bg-[#D90007] rounded-full transition-all duration-300"
      style={{ width: `${percent}%` }}
    />
  </div>
);

const SectionHeader = ({ children }) => (
  <div className="text-[10px] font-bold text-[#666] uppercase tracking-wider px-4 py-2">
    {children}
  </div>
);

const Table = ({ headers, rows, selectedIndex, onSelect }) => (
  <div className="border border-[#E0E0E0] rounded-md overflow-hidden">
    <div className="bg-[#FAFAFA] border-b border-[#E8E8E8]">
      <div className="flex">
        {headers.map((h, i) => (
          <div key={i} className={`px-3 py-2 text-xs font-bold text-[#333] ${h.width || 'flex-1'}`}>
            {h.label}
          </div>
        ))}
      </div>
    </div>
    <div className="max-h-48 overflow-y-auto">
      {rows.map((row, i) => (
        <div 
          key={i}
          onClick={() => onSelect?.(i)}
          className={`
            flex border-b border-[#E8E8E8] last:border-b-0 cursor-pointer transition-colors
            ${selectedIndex === i ? 'bg-[#FFE6E7]' : i % 2 === 0 ? 'bg-white' : 'bg-[#FAFAFA]'}
            hover:bg-[#FFE6E7]/50
          `}
        >
          {row.map((cell, j) => (
            <div key={j} className={`px-3 py-2 text-sm text-[#333] ${headers[j]?.width || 'flex-1'}`}>
              {cell}
            </div>
          ))}
        </div>
      ))}
    </div>
  </div>
);

const Tabs = ({ tabs, active, onChange }) => (
  <div className="flex border-b border-[#E0E0E0]">
    {tabs.map((tab) => (
      <button
        key={tab.id}
        onClick={() => onChange(tab.id)}
        className={`
          px-4 py-2 text-sm font-medium transition-all border-b-2 -mb-[1px]
          ${active === tab.id 
            ? 'text-[#D90007] border-[#D90007]' 
            : 'text-[#666] border-transparent hover:text-[#333] hover:bg-[#F5F5F5]'
          }
        `}
      >
        {tab.label}
      </button>
    ))}
  </div>
);

// ============ SIDEBAR ============

const Sidebar = ({ activePanel, setActivePanel, cloudFolders }) => {
  const [expandedFolders, setExpandedFolders] = useState(['Cloud Drive']);

  const NavItem = ({ icon, label, id, indent = 0 }) => {
    const isActive = activePanel === id;
    return (
      <button
        onClick={() => setActivePanel(id)}
        className={`
          w-full flex items-center gap-2 px-4 py-2 text-sm transition-all text-left
          ${isActive 
            ? 'bg-[#FFE6E7] text-[#D90007] font-semibold' 
            : 'text-[#444] hover:bg-[#F0F0F0]'
          }
        `}
        style={{ paddingLeft: `${16 + indent * 16}px` }}
      >
        <span className="w-5 h-5 flex items-center justify-center">{icon}</span>
        {label}
      </button>
    );
  };

  return (
    <div className="w-[240px] bg-[#FAFAFA] border-r border-[#E0E0E0] flex flex-col h-full">
      {/* Logo */}
      <div className="px-4 py-4 border-b border-[#E0E0E0]">
        <div className="flex items-center gap-2">
          <div className="w-8 h-8 bg-[#D90007] rounded-lg flex items-center justify-center">
            <span className="text-white font-bold text-lg">M</span>
          </div>
          <span className="text-xl font-bold text-[#333]">MegaCustom</span>
        </div>
      </div>

      {/* Cloud Drive Section */}
      <div className="flex-1 overflow-y-auto">
        <div className="py-2">
          <div className="flex items-center justify-between px-4 py-1">
            <NavItem icon={<Cloud size={18} />} label="Cloud Drive" id="cloud" />
          </div>
          
          {/* Folder Tree */}
          <div className="ml-6 border-l border-[#E0E0E0] pl-2 mt-1">
            {cloudFolders.map((folder, i) => (
              <button
                key={i}
                className="w-full flex items-center gap-2 px-2 py-1.5 text-sm text-[#555] hover:bg-[#F0F0F0] rounded text-left"
              >
                <Folder size={14} className="text-[#999]" />
                {folder}
              </button>
            ))}
          </div>
        </div>

        <SectionHeader>Tools</SectionHeader>
        <NavItem icon={<FolderSync size={18} />} label="Folder Mapper" id="mapper" />
        <NavItem icon={<Upload size={18} />} label="Multi Uploader" id="uploader" />
        <NavItem icon={<Zap size={18} />} label="Smart Sync" id="sync" />

        <div className="mt-4">
          <NavItem icon={<HardDrive size={18} />} label="Transfers" id="transfers" />
          <NavItem icon={<Settings size={18} />} label="Settings" id="settings" />
        </div>
      </div>

      {/* Storage Info */}
      <div className="p-4 border-t border-[#E0E0E0]">
        <div className="text-xs text-[#666] mb-2">Storage Used</div>
        <ProgressBar percent={35} />
        <div className="text-xs text-[#666] mt-1">35 GB of 100 GB</div>
      </div>
    </div>
  );
};

// ============ TOP TOOLBAR ============

const TopToolbar = ({ currentPath, viewMode, setViewMode, onSearch }) => {
  const [searchQuery, setSearchQuery] = useState('');

  return (
    <div className="h-[60px] bg-white border-b border-[#E0E0E0] flex items-center px-4 gap-4">
      {/* Navigation */}
      <div className="flex gap-1">
        <IconButton icon={<ArrowLeft size={18} />} title="Back" />
        <IconButton icon={<ArrowRight size={18} />} title="Forward" />
        <IconButton icon={<ArrowUp size={18} />} title="Up" />
      </div>

      {/* Breadcrumb */}
      <div className="flex items-center gap-1 text-sm flex-1">
        {currentPath.map((segment, i) => (
          <React.Fragment key={i}>
            {i > 0 && <ChevronRight size={14} className="text-[#999]" />}
            <button 
              className={`
                px-2 py-1 rounded hover:bg-[#F0F0F0] transition-colors
                ${i === currentPath.length - 1 ? 'font-semibold text-[#333]' : 'text-[#666]'}
              `}
            >
              {segment}
            </button>
          </React.Fragment>
        ))}
      </div>

      {/* Search */}
      <Input 
        placeholder="Search files..." 
        value={searchQuery}
        onChange={(e) => setSearchQuery(e.target.value)}
        icon={<Search size={16} />}
        className="w-64"
      />

      {/* Actions */}
      <div className="flex gap-2">
        <Button primary icon={<Upload size={16} />}>Upload</Button>
        <Button icon={<FolderPlus size={16} />}>New Folder</Button>
        <IconButton icon={<Trash2 size={18} />} title="Delete" />
        <IconButton icon={<RefreshCw size={18} />} title="Refresh" />
      </div>

      {/* View Mode */}
      <div className="flex border border-[#E0E0E0] rounded-md overflow-hidden">
        <IconButton icon={<List size={18} />} active={viewMode === 'list'} onClick={() => setViewMode('list')} />
        <IconButton icon={<Grid size={18} />} active={viewMode === 'grid'} onClick={() => setViewMode('grid')} />
        <IconButton icon={<LayoutGrid size={18} />} active={viewMode === 'detail'} onClick={() => setViewMode('detail')} />
      </div>
    </div>
  );
};

// ============ CLOUD DRIVE PANEL ============

const CloudDrivePanel = ({ viewMode }) => {
  const [selectedFile, setSelectedFile] = useState(null);
  
  const files = [
    { name: 'Documents', type: 'folder', size: '-', modified: '2024-01-15' },
    { name: 'Photos', type: 'folder', size: '-', modified: '2024-01-14' },
    { name: 'Backups', type: 'folder', size: '-', modified: '2024-01-10' },
    { name: 'project-spec.pdf', type: 'pdf', size: '2.4 MB', modified: '2024-01-15' },
    { name: 'presentation.pptx', type: 'file', size: '15.8 MB', modified: '2024-01-14' },
    { name: 'screenshot.png', type: 'image', size: '856 KB', modified: '2024-01-13' },
    { name: 'archive.zip', type: 'archive', size: '45.2 MB', modified: '2024-01-12' },
    { name: 'notes.txt', type: 'text', size: '12 KB', modified: '2024-01-11' },
  ];

  const getIcon = (type) => {
    switch(type) {
      case 'folder': return <Folder size={20} className="text-[#FFB900]" />;
      case 'pdf': return <FileText size={20} className="text-[#D90007]" />;
      case 'image': return <Image size={20} className="text-[#4CAF50]" />;
      case 'archive': return <Archive size={20} className="text-[#9C27B0]" />;
      case 'text': return <FileText size={20} className="text-[#2196F3]" />;
      default: return <File size={20} className="text-[#666]" />;
    }
  };

  if (viewMode === 'grid') {
    return (
      <div className="p-4 grid grid-cols-6 gap-4">
        {files.map((file, i) => (
          <div
            key={i}
            onClick={() => setSelectedFile(i)}
            className={`
              p-4 rounded-lg border-2 cursor-pointer transition-all text-center
              ${selectedFile === i 
                ? 'border-[#D90007] bg-[#FFE6E7]' 
                : 'border-transparent hover:bg-[#F5F5F5]'
              }
            `}
          >
            <div className="w-12 h-12 mx-auto mb-2 flex items-center justify-center">
              {React.cloneElement(getIcon(file.type), { size: 32 })}
            </div>
            <div className="text-sm text-[#333] truncate">{file.name}</div>
            <div className="text-xs text-[#999]">{file.size}</div>
          </div>
        ))}
      </div>
    );
  }

  return (
    <div className="p-4">
      <Table
        headers={[
          { label: 'Name', width: 'flex-[2]' },
          { label: 'Size', width: 'w-24' },
          { label: 'Modified', width: 'w-32' },
        ]}
        rows={files.map(f => [
          <div className="flex items-center gap-2">
            {getIcon(f.type)}
            <span>{f.name}</span>
          </div>,
          f.size,
          f.modified
        ])}
        selectedIndex={selectedFile}
        onSelect={setSelectedFile}
      />
    </div>
  );
};

// ============ FOLDER MAPPER PANEL ============

const FolderMapperPanel = () => {
  const [mappings, setMappings] = useState([
    { name: 'Server Logs', local: '/var/log', remote: '/Backups/Logs', status: 'Ready' },
    { name: 'Database Dumps', local: '/data/dumps', remote: '/Backups/DB', status: 'Ready' },
    { name: 'Config Files', local: '/etc/app', remote: '/Backups/Config', status: 'Syncing' },
  ]);
  const [selectedMapping, setSelectedMapping] = useState(0);
  const [progress, setProgress] = useState(45);

  return (
    <div className="p-4 space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold text-[#333]">Folder Mapper</h2>
          <p className="text-sm text-[#666]">VPS-to-MEGA Upload Tool — Map local folders to cloud destinations</p>
        </div>
      </div>

      {/* Action Buttons */}
      <div className="flex gap-2">
        <Button icon={<Plus size={16} />}>Add</Button>
        <Button icon={<Edit size={16} />}>Update</Button>
        <Button icon={<Minus size={16} />}>Remove</Button>
        <Button icon={<RefreshCw size={16} />}>Refresh</Button>
      </div>

      {/* Input Form */}
      <div className="grid grid-cols-3 gap-4 p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <div>
          <label className="text-xs font-medium text-[#666] mb-1 block">Name</label>
          <Input placeholder="Mapping name" value="New Mapping" />
        </div>
        <div>
          <label className="text-xs font-medium text-[#666] mb-1 block">Local Path</label>
          <div className="flex gap-2">
            <Input placeholder="/path/to/local" className="flex-1" />
            <Button>Browse</Button>
          </div>
        </div>
        <div>
          <label className="text-xs font-medium text-[#666] mb-1 block">Remote Path</label>
          <div className="flex gap-2">
            <Input placeholder="/cloud/path" className="flex-1" />
            <Button>Browse</Button>
          </div>
        </div>
      </div>

      {/* Mappings Table */}
      <div>
        <h3 className="text-sm font-semibold text-[#333] mb-2">Mappings</h3>
        <Table
          headers={[
            { label: 'Name' },
            { label: 'Local Path' },
            { label: 'Remote Path' },
            { label: 'Status', width: 'w-24' },
          ]}
          rows={mappings.map(m => [
            m.name,
            <code className="text-xs bg-[#F0F0F0] px-1 rounded">{m.local}</code>,
            <code className="text-xs bg-[#F0F0F0] px-1 rounded">{m.remote}</code>,
            <span className={`text-xs px-2 py-1 rounded ${
              m.status === 'Ready' ? 'bg-green-100 text-green-700' : 'bg-blue-100 text-blue-700'
            }`}>{m.status}</span>
          ])}
          selectedIndex={selectedMapping}
          onSelect={setSelectedMapping}
        />
      </div>

      {/* Actions */}
      <div className="flex gap-2">
        <Button icon={<Eye size={16} />}>Preview</Button>
        <Button primary icon={<Upload size={16} />}>Upload All</Button>
        <Button icon={<RefreshCw size={16} />}>Refresh</Button>
      </div>

      {/* Progress Section */}
      <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <div className="flex justify-between text-sm mb-2">
          <span className="font-medium">Progress</span>
          <span className="text-[#666]">{progress}%</span>
        </div>
        <ProgressBar percent={progress} className="mb-3" />
        <div className="grid grid-cols-3 gap-4 text-sm">
          <div><span className="text-[#666]">Current:</span> processing_file.log</div>
          <div><span className="text-[#666]">Files:</span> 45/100</div>
          <div><span className="text-[#666]">Speed:</span> 2.5 MB/s</div>
        </div>
      </div>

      {/* Settings */}
      <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <h3 className="text-sm font-semibold text-[#333] mb-3">Settings</h3>
        <div className="space-y-3">
          <Checkbox checked={true} label="Incremental (only new/modified files)" />
          <Checkbox checked={true} label="Recursive (include subfolders)" />
          <div className="flex items-center gap-4">
            <span className="text-sm text-[#666]">Concurrent uploads:</span>
            <Input value="3" className="w-16" />
          </div>
          <div className="flex items-center gap-4">
            <span className="text-sm text-[#666]">Exclude patterns:</span>
            <Input value="*.tmp, *.temp, .DS_Store" className="flex-1" />
          </div>
        </div>
      </div>
    </div>
  );
};

// ============ MULTI UPLOADER PANEL ============

const MultiUploaderPanel = () => {
  const [sourceFiles] = useState([
    { name: 'document.pdf', size: '2.4 MB' },
    { name: 'image.jpg', size: '1.8 MB' },
    { name: 'spreadsheet.xlsx', size: '1.3 MB' },
  ]);

  const [destinations] = useState([
    '/Cloud/Projects',
    '/Cloud/Archive',
  ]);

  const [rules] = useState([
    { pattern: '*.pdf', destination: '/Cloud/Documents' },
    { pattern: '*.jpg, *.png', destination: '/Cloud/Media' },
    { pattern: '*.xlsx', destination: '/Cloud/Spreadsheets' },
  ]);

  const [tasks] = useState([
    { file: 'document.pdf', dest: '/Documents', status: 'completed' },
    { file: 'image.jpg', dest: '/Media', status: 'uploading', progress: 67 },
    { file: 'spreadsheet.xlsx', dest: '/Spreadsheets', status: 'pending' },
  ]);

  return (
    <div className="p-4 space-y-4">
      <div>
        <h2 className="text-lg font-semibold text-[#333]">Multi Uploader</h2>
        <p className="text-sm text-[#666]">Upload files to multiple destinations with distribution rules</p>
      </div>

      <div className="grid grid-cols-2 gap-4">
        {/* Source Files */}
        <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
          <h3 className="text-sm font-semibold text-[#333] mb-3">Source Files</h3>
          <div className="space-y-2 mb-3">
            {sourceFiles.map((f, i) => (
              <div key={i} className="flex items-center gap-2 p-2 bg-white rounded border border-[#E8E8E8]">
                <File size={16} className="text-[#666]" />
                <span className="text-sm flex-1">{f.name}</span>
                <span className="text-xs text-[#999]">{f.size}</span>
                <button className="text-[#999] hover:text-[#D90007]"><X size={14} /></button>
              </div>
            ))}
          </div>
          <div className="flex gap-2">
            <Button icon={<Plus size={14} />}>Add Files</Button>
            <Button icon={<FolderPlus size={14} />}>Add Folder</Button>
            <Button>Clear</Button>
          </div>
          <div className="text-xs text-[#666] mt-2">Total: {sourceFiles.length} files | 5.5 MB</div>
        </div>

        {/* Destinations */}
        <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
          <h3 className="text-sm font-semibold text-[#333] mb-3">Destinations</h3>
          <div className="space-y-2 mb-3">
            {destinations.map((d, i) => (
              <div key={i} className="flex items-center gap-2 p-2 bg-white rounded border border-[#E8E8E8]">
                <Folder size={16} className="text-[#FFB900]" />
                <span className="text-sm flex-1">{d}</span>
                <button className="text-[#999] hover:text-[#D90007]"><X size={14} /></button>
              </div>
            ))}
          </div>
          <div className="flex gap-2">
            <Button icon={<Plus size={14} />}>Add</Button>
            <Button icon={<Minus size={14} />}>Remove</Button>
          </div>
        </div>
      </div>

      {/* Distribution Rules */}
      <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-sm font-semibold text-[#333]">Distribution Rules</h3>
          <div className="flex items-center gap-2">
            <span className="text-xs text-[#666]">Rule Type:</span>
            <select className="text-sm border border-[#E0E0E0] rounded px-2 py-1">
              <option>By File Type</option>
              <option>By File Size</option>
              <option>By Date</option>
            </select>
          </div>
        </div>
        <Table
          headers={[
            { label: 'Pattern' },
            { label: 'Destination' },
          ]}
          rows={rules.map(r => [
            <code className="text-xs bg-[#F0F0F0] px-1 rounded">{r.pattern}</code>,
            r.destination
          ])}
        />
        <div className="flex gap-2 mt-3">
          <Button icon={<Plus size={14} />}>Add Rule</Button>
          <Button icon={<Minus size={14} />}>Remove Rule</Button>
        </div>
      </div>

      {/* Upload Tasks */}
      <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <h3 className="text-sm font-semibold text-[#333] mb-3">Upload Tasks</h3>
        <Table
          headers={[
            { label: 'File' },
            { label: 'Destination' },
            { label: 'Status' },
          ]}
          rows={tasks.map(t => [
            t.file,
            t.dest,
            <div className="flex items-center gap-2">
              {t.status === 'completed' && <Check size={14} className="text-green-600" />}
              {t.status === 'uploading' && (
                <div className="flex items-center gap-2 flex-1">
                  <ProgressBar percent={t.progress} className="w-20" />
                  <span className="text-xs">{t.progress}%</span>
                </div>
              )}
              {t.status === 'pending' && <Clock size={14} className="text-[#999]" />}
              <span className="text-xs capitalize">{t.status}</span>
            </div>
          ])}
        />
        <div className="flex gap-2 mt-3">
          <Button primary icon={<Play size={14} />}>Start</Button>
          <Button icon={<Pause size={14} />}>Pause</Button>
          <Button icon={<X size={14} />}>Cancel</Button>
        </div>
      </div>
    </div>
  );
};

// ============ SMART SYNC PANEL ============

const SmartSyncPanel = () => {
  const [activeTab, setActiveTab] = useState('preview');
  const [profiles] = useState([
    { name: 'Projects', local: '~/Documents/Projects', remote: '/Cloud/Projects' },
    { name: 'Photos', local: '~/Pictures', remote: '/Cloud/Photos' },
  ]);
  const [selectedProfile, setSelectedProfile] = useState(0);

  const previewItems = [
    { file: 'report.pdf', action: 'Upload', status: 'Pending' },
    { file: 'photo.jpg', action: 'Download', status: 'Pending' },
    { file: 'data.xlsx', action: 'Skip', status: 'Unchanged' },
  ];

  const conflicts = [
    { file: 'notes.txt', local: '2024-01-15 10:30', remote: '2024-01-15 11:45', resolution: 'Keep Newer' },
  ];

  return (
    <div className="p-4 space-y-4">
      <div>
        <h2 className="text-lg font-semibold text-[#333]">Smart Sync</h2>
        <p className="text-sm text-[#666]">Bidirectional folder synchronization with conflict resolution</p>
      </div>

      {/* Profiles */}
      <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <h3 className="text-sm font-semibold text-[#333] mb-3">Sync Profiles</h3>
        <Table
          headers={[
            { label: 'Name' },
            { label: 'Local Path' },
            { label: 'Remote Path' },
          ]}
          rows={profiles.map(p => [
            p.name,
            <code className="text-xs bg-[#F0F0F0] px-1 rounded">{p.local}</code>,
            <code className="text-xs bg-[#F0F0F0] px-1 rounded">{p.remote}</code>,
          ])}
          selectedIndex={selectedProfile}
          onSelect={setSelectedProfile}
        />
        <div className="flex gap-2 mt-3">
          <Button icon={<Plus size={14} />}>New</Button>
          <Button icon={<Edit size={14} />}>Edit</Button>
          <Button icon={<Trash2 size={14} />}>Delete</Button>
        </div>
      </div>

      {/* Configuration */}
      <div className="p-4 bg-[#FAFAFA] rounded-lg border border-[#E0E0E0]">
        <h3 className="text-sm font-semibold text-[#333] mb-3">Configuration</h3>
        <div className="grid grid-cols-2 gap-4">
          <div>
            <label className="text-xs font-medium text-[#666] mb-1 block">Direction</label>
            <select className="w-full h-9 px-3 rounded-md border border-[#E0E0E0] bg-white text-sm">
              <option>Bidirectional</option>
              <option>Local to Remote only</option>
              <option>Remote to Local only</option>
            </select>
          </div>
          <div>
            <label className="text-xs font-medium text-[#666] mb-1 block">Conflict Resolution</label>
            <select className="w-full h-9 px-3 rounded-md border border-[#E0E0E0] bg-white text-sm">
              <option>Keep Newer</option>
              <option>Keep Larger</option>
              <option>Keep Local</option>
              <option>Keep Remote</option>
              <option>Keep Both (rename)</option>
              <option>Ask User</option>
            </select>
          </div>
          <div>
            <label className="text-xs font-medium text-[#666] mb-1 block">Include Patterns</label>
            <Input value="*.txt, *.doc, *.pdf" />
          </div>
          <div>
            <label className="text-xs font-medium text-[#666] mb-1 block">Exclude Patterns</label>
            <Input value="*.tmp, .git, node_modules" />
          </div>
        </div>
        <div className="flex items-center gap-4 mt-4">
          <Checkbox checked={true} label="Auto-sync" />
          <span className="text-sm text-[#666]">Interval:</span>
          <Input value="60" className="w-16" />
          <span className="text-sm text-[#666]">minutes</span>
        </div>
      </div>

      {/* Action Buttons */}
      <div className="flex gap-2">
        <Button icon={<Eye size={14} />}>Analyze</Button>
        <Button primary icon={<Play size={14} />}>Start Sync</Button>
        <Button icon={<Pause size={14} />}>Pause</Button>
        <Button icon={<X size={14} />}>Stop</Button>
      </div>

      {/* Tabs Content */}
      <div className="border border-[#E0E0E0] rounded-lg overflow-hidden">
        <Tabs
          tabs={[
            { id: 'preview', label: 'Preview' },
            { id: 'conflicts', label: 'Conflicts (1)' },
            { id: 'progress', label: 'Progress' },
          ]}
          active={activeTab}
          onChange={setActiveTab}
        />
        <div className="p-4">
          {activeTab === 'preview' && (
            <Table
              headers={[
                { label: 'File' },
                { label: 'Action' },
                { label: 'Status' },
              ]}
              rows={previewItems.map(item => [
                item.file,
                <span className={`text-xs px-2 py-1 rounded ${
                  item.action === 'Upload' ? 'bg-blue-100 text-blue-700' :
                  item.action === 'Download' ? 'bg-green-100 text-green-700' :
                  'bg-gray-100 text-gray-700'
                }`}>{item.action}</span>,
                item.status
              ])}
            />
          )}
          {activeTab === 'conflicts' && (
            <Table
              headers={[
                { label: 'File' },
                { label: 'Local Modified' },
                { label: 'Remote Modified' },
                { label: 'Resolution' },
              ]}
              rows={conflicts.map(c => [
                <div className="flex items-center gap-2">
                  <AlertCircle size={14} className="text-amber-500" />
                  {c.file}
                </div>,
                c.local,
                c.remote,
                <select className="text-xs border rounded px-1 py-0.5">
                  <option>{c.resolution}</option>
                </select>
              ])}
            />
          )}
          {activeTab === 'progress' && (
            <div className="space-y-4">
              <ProgressBar percent={45} />
              <div className="grid grid-cols-3 gap-4 text-sm">
                <div><span className="text-[#666]">Status:</span> Syncing...</div>
                <div><span className="text-[#666]">Files:</span> 23/50</div>
                <div><span className="text-[#666]">Speed:</span> 1.8 MB/s</div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

// ============ TRANSFERS PANEL ============

const TransfersPanel = () => {
  const [transfers] = useState([
    { file: 'project.zip', type: 'download', progress: 50, speed: '2.5 MB/s' },
    { file: 'photo.jpg', type: 'upload', progress: 75, speed: '1.8 MB/s' },
    { file: 'document.pdf', type: 'upload', progress: 100, speed: '-' },
    { file: 'backup.tar.gz', type: 'upload', progress: 100, speed: '-' },
    { file: 'data.csv', type: 'download', progress: 30, speed: '3.2 MB/s' },
  ]);

  const active = transfers.filter(t => t.progress < 100);
  const completed = transfers.filter(t => t.progress === 100);

  return (
    <div className="p-4 space-y-4">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold text-[#333]">Transfers</h2>
          <p className="text-sm text-[#666]">
            {active.length} Active | {completed.length} Completed
          </p>
        </div>
        <div className="flex gap-2">
          <Button icon={<X size={14} />}>Cancel All</Button>
          <Button icon={<Trash2 size={14} />}>Clear Completed</Button>
        </div>
      </div>

      <Table
        headers={[
          { label: 'File', width: 'flex-[2]' },
          { label: 'Type', width: 'w-24' },
          { label: 'Progress', width: 'flex-1' },
          { label: 'Speed', width: 'w-24' },
          { label: 'Actions', width: 'w-20' },
        ]}
        rows={transfers.map(t => [
          <div className="flex items-center gap-2">
            <File size={16} className="text-[#666]" />
            {t.file}
          </div>,
          <span className={`text-xs px-2 py-1 rounded ${
            t.type === 'upload' ? 'bg-blue-100 text-blue-700' : 'bg-green-100 text-green-700'
          }`}>
            {t.type === 'upload' ? <Upload size={12} className="inline mr-1" /> : <Download size={12} className="inline mr-1" />}
            {t.type}
          </span>,
          <div className="flex items-center gap-2">
            <ProgressBar percent={t.progress} className="flex-1" />
            <span className="text-xs w-10">{t.progress}%</span>
          </div>,
          t.speed,
          t.progress < 100 ? (
            <div className="flex gap-1">
              <button className="p-1 hover:bg-[#F0F0F0] rounded"><Pause size={14} /></button>
              <button className="p-1 hover:bg-[#F0F0F0] rounded text-[#D90007]"><X size={14} /></button>
            </div>
          ) : (
            <Check size={16} className="text-green-600" />
          )
        ])}
      />
    </div>
  );
};

// ============ SETTINGS PANEL ============

const SettingsPanel = () => {
  const [activeTab, setActiveTab] = useState('general');

  return (
    <div className="p-4">
      <h2 className="text-lg font-semibold text-[#333] mb-4">Settings</h2>
      
      <div className="flex gap-6">
        {/* Sidebar */}
        <div className="w-48 space-y-1">
          {['General', 'Sync', 'Network', 'Advanced'].map(tab => (
            <button
              key={tab}
              onClick={() => setActiveTab(tab.toLowerCase())}
              className={`
                w-full text-left px-3 py-2 rounded-md text-sm transition-colors
                ${activeTab === tab.toLowerCase() 
                  ? 'bg-[#FFE6E7] text-[#D90007] font-medium' 
                  : 'text-[#666] hover:bg-[#F0F0F0]'
                }
              `}
            >
              {tab}
            </button>
          ))}
        </div>

        {/* Content */}
        <div className="flex-1 space-y-6">
          {activeTab === 'general' && (
            <>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Startup</h3>
                <Checkbox checked={true} label="Start MegaCustom at login" />
                <Checkbox checked={true} label="Show system tray icon" />
                <Checkbox checked={false} label="Start minimized" />
              </div>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Appearance</h3>
                <Checkbox checked={false} label="Dark mode" />
                <div className="flex items-center gap-4">
                  <span className="text-sm text-[#666]">Language:</span>
                  <select className="h-9 px-3 rounded-md border border-[#E0E0E0] bg-white text-sm">
                    <option>English</option>
                    <option>Spanish</option>
                    <option>German</option>
                  </select>
                </div>
              </div>
            </>
          )}

          {activeTab === 'sync' && (
            <>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Default Settings</h3>
                <div className="flex items-center gap-4">
                  <span className="text-sm text-[#666]">Auto-sync interval:</span>
                  <Input value="60" className="w-20" />
                  <span className="text-sm text-[#666]">minutes</span>
                </div>
                <div className="flex items-center gap-4">
                  <span className="text-sm text-[#666]">Default conflict resolution:</span>
                  <select className="h-9 px-3 rounded-md border border-[#E0E0E0] bg-white text-sm">
                    <option>Keep Newer</option>
                    <option>Ask User</option>
                  </select>
                </div>
              </div>
            </>
          )}

          {activeTab === 'network' && (
            <>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Speed Limits</h3>
                <div className="flex items-center gap-4">
                  <Checkbox checked={false} label="Limit upload speed" />
                  <Input value="0" className="w-20" />
                  <span className="text-sm text-[#666]">KB/s</span>
                </div>
                <div className="flex items-center gap-4">
                  <Checkbox checked={false} label="Limit download speed" />
                  <Input value="0" className="w-20" />
                  <span className="text-sm text-[#666]">KB/s</span>
                </div>
              </div>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Parallel Transfers</h3>
                <div className="flex items-center gap-4">
                  <span className="text-sm text-[#666]">Maximum concurrent transfers:</span>
                  <Input value="4" className="w-20" />
                </div>
              </div>
            </>
          )}

          {activeTab === 'advanced' && (
            <>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Exclude Patterns</h3>
                <Input value="*.tmp, *.temp, .DS_Store, Thumbs.db" className="w-full" />
                <p className="text-xs text-[#999]">Comma-separated patterns to exclude from sync</p>
              </div>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Cache</h3>
                <div className="flex items-center gap-4">
                  <span className="text-sm text-[#666]">Cache size limit:</span>
                  <Input value="500" className="w-20" />
                  <span className="text-sm text-[#666]">MB</span>
                </div>
                <Button>Clear Cache</Button>
              </div>
              <div className="space-y-4">
                <h3 className="text-sm font-semibold text-[#333]">Logging</h3>
                <div className="flex items-center gap-4">
                  <span className="text-sm text-[#666]">Log level:</span>
                  <select className="h-9 px-3 rounded-md border border-[#E0E0E0] bg-white text-sm">
                    <option>Info</option>
                    <option>Debug</option>
                    <option>Warning</option>
                    <option>Error</option>
                  </select>
                </div>
              </div>
            </>
          )}
        </div>
      </div>
    </div>
  );
};

// ============ LOGIN DIALOG ============

const LoginDialog = ({ onLogin }) => (
  <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
    <div className="bg-white rounded-xl shadow-2xl w-[380px] p-8">
      <div className="text-center mb-6">
        <div className="w-16 h-16 bg-[#D90007] rounded-2xl flex items-center justify-center mx-auto mb-4">
          <span className="text-white font-bold text-3xl">M</span>
        </div>
        <h2 className="text-xl font-semibold text-[#333]">MegaCustom Login</h2>
        <p className="text-sm text-[#666] mt-1">Sign in to your MEGA account</p>
      </div>
      
      <div className="space-y-4">
        <div>
          <label className="text-sm font-medium text-[#666] mb-1 block">Email</label>
          <Input placeholder="you@example.com" />
        </div>
        <div>
          <label className="text-sm font-medium text-[#666] mb-1 block">Password</label>
          <input
            type="password"
            placeholder="••••••••"
            className="w-full h-10 px-3 rounded-md border border-[#E0E0E0] bg-white text-sm focus:outline-none focus:border-[#D90007] focus:ring-2 focus:ring-[#D90007]/20"
          />
        </div>
        <Checkbox checked={true} label="Remember me" />
      </div>

      <div className="flex gap-3 mt-6">
        <Button className="flex-1">Cancel</Button>
        <Button primary className="flex-1" onClick={onLogin}>Login</Button>
      </div>
    </div>
  </div>
);

// ============ STATUS BAR ============

const StatusBar = () => (
  <div className="h-7 bg-[#FAFAFA] border-t border-[#E0E0E0] flex items-center px-4 text-xs text-[#666]">
    <div className="flex items-center gap-2">
      <div className="w-2 h-2 rounded-full bg-green-500"></div>
      <span>Connected</span>
    </div>
    <div className="mx-4">|</div>
    <span>user@example.com</span>
    <div className="flex-1"></div>
    <span>↑ 1.2 MB/s</span>
    <div className="mx-2">|</div>
    <span>↓ 2.5 MB/s</span>
  </div>
);

// ============ MAIN APP ============

export default function MegaCustomApp() {
  const [showLogin, setShowLogin] = useState(true);
  const [activePanel, setActivePanel] = useState('cloud');
  const [viewMode, setViewMode] = useState('detail');

  const cloudFolders = ['Documents', 'Photos', 'Backups', 'Projects'];
  const currentPath = ['Cloud Drive', 'Documents', 'Projects'];

  if (showLogin) {
    return <LoginDialog onLogin={() => setShowLogin(false)} />;
  }

  const renderPanel = () => {
    switch (activePanel) {
      case 'cloud': return <CloudDrivePanel viewMode={viewMode} />;
      case 'mapper': return <FolderMapperPanel />;
      case 'uploader': return <MultiUploaderPanel />;
      case 'sync': return <SmartSyncPanel />;
      case 'transfers': return <TransfersPanel />;
      case 'settings': return <SettingsPanel />;
      default: return <CloudDrivePanel viewMode={viewMode} />;
    }
  };

  return (
    <div className="w-full h-[700px] bg-white rounded-lg shadow-xl overflow-hidden flex flex-col border border-[#E0E0E0]">
      {/* Menu Bar */}
      <div className="h-8 bg-[#FAFAFA] border-b border-[#E0E0E0] flex items-center px-4 text-sm text-[#555]">
        {['File', 'Edit', 'View', 'Tools', 'Help'].map(menu => (
          <button key={menu} className="px-3 py-1 hover:bg-[#E8E8E8] rounded">
            {menu}
          </button>
        ))}
      </div>

      {/* Top Toolbar */}
      <TopToolbar 
        currentPath={currentPath}
        viewMode={viewMode}
        setViewMode={setViewMode}
      />

      {/* Main Content */}
      <div className="flex flex-1 overflow-hidden">
        <Sidebar 
          activePanel={activePanel}
          setActivePanel={setActivePanel}
          cloudFolders={cloudFolders}
        />
        <div className="flex-1 overflow-y-auto bg-white">
          {renderPanel()}
        </div>
      </div>

      {/* Status Bar */}
      <StatusBar />
    </div>
  );
}

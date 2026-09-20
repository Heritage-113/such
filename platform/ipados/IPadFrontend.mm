#include <such/ui/FrontendLease.h>
#include <such/ui/FontSettings.h>
#include <such/ui/ResultView.h>
#include <such/ui/SearchDialect.h>

#import <UIKit/UIKit.h>

#include <ctime>
#include <optional>
#include <vector>

namespace {
std::optional<such::ui::FrontendLease> gLease;
UIColor* paperColor() { return [UIColor colorWithRed:243.0/255.0 green:239.0/255.0 blue:229.0/255.0 alpha:1.0]; }
UIColor* brightColor() { return [UIColor colorWithRed:251.0/255.0 green:248.0/255.0 blue:239.0/255.0 alpha:1.0]; }
UIColor* darkGreen() { return [UIColor colorWithRed:23.0/255.0 green:58.0/255.0 blue:43.0/255.0 alpha:1.0]; }
UIColor* pathGreen() { return [UIColor colorWithRed:84.0/255.0 green:103.0/255.0 blue:91.0/255.0 alpha:1.0]; }
NSString* ns(const std::string& s) {
    NSString* value = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    return value != nil ? value : @"";
}
}

static NSString* gSuchFontFamily = nil;

static UIFont* suchFont(CGFloat size, UIFontWeight weight) {
    if (gSuchFontFamily.length > 0) {
        UIFont* custom = [UIFont fontWithName:gSuchFontFamily size:size];
        if (custom != nil) return custom;
    }
    return [UIFont systemFontOfSize:size weight:weight];
}

@interface SuchCell : UITableViewCell
- (void)applyItem:(const such::ui::ResultItem&)item;
@end

@implementation SuchCell {
    UIImageView* _fileIcon;
    UILabel* _badge;
    UILabel* _filename;
    UILabel* _path;
    UILabel* _pin;
}
- (instancetype)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString*)reuseIdentifier {
    self = [super initWithStyle:style reuseIdentifier:reuseIdentifier];
    if (!self) return nil;
    self.backgroundColor = UIColor.clearColor;
    self.selectionStyle = UITableViewCellSelectionStyleNone;

    UIView* card = [[UIView alloc] initWithFrame:CGRectZero];
    card.translatesAutoresizingMaskIntoConstraints = NO;
    card.backgroundColor = brightColor();
    card.layer.cornerRadius = 5.0;
    card.layer.borderWidth = 0.5;
    card.layer.borderColor = [UIColor colorWithRed:211.0/255.0 green:205.0/255.0 blue:191.0/255.0 alpha:1.0].CGColor;
    [self.contentView addSubview:card];

    _fileIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    _fileIcon.translatesAutoresizingMaskIntoConstraints = NO;
    _fileIcon.contentMode = UIViewContentModeScaleAspectFit;
    [card addSubview:_fileIcon];

    _badge = [[UILabel alloc] initWithFrame:CGRectZero];
    _badge.translatesAutoresizingMaskIntoConstraints = NO;
    _badge.backgroundColor = darkGreen();
    _badge.textColor = UIColor.whiteColor;
    _badge.font = suchFont(9.0, UIFontWeightBold);
    _badge.textAlignment = NSTextAlignmentCenter;
    _badge.layer.cornerRadius = 3.0;
    _badge.clipsToBounds = YES;
    [card addSubview:_badge];

    _filename = [[UILabel alloc] initWithFrame:CGRectZero];
    _filename.translatesAutoresizingMaskIntoConstraints = NO;
    _filename.textColor = darkGreen();
    _filename.font = suchFont(15.0, UIFontWeightSemibold);
    _filename.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [card addSubview:_filename];

    _path = [[UILabel alloc] initWithFrame:CGRectZero];
    _path.translatesAutoresizingMaskIntoConstraints = NO;
    _path.textColor = pathGreen();
    _path.font = suchFont(11.5, UIFontWeightRegular);
    _path.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [card addSubview:_path];

    _pin = [[UILabel alloc] initWithFrame:CGRectZero];
    _pin.translatesAutoresizingMaskIntoConstraints = NO;
    _pin.font = suchFont(12.0, UIFontWeightRegular);
    _pin.text = @"📌";
    [card addSubview:_pin];

    [NSLayoutConstraint activateConstraints:@[
        [card.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor],
        [card.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor],
        [card.topAnchor constraintEqualToAnchor:self.contentView.topAnchor constant:3.5],
        [card.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor constant:-3.5],
        [_fileIcon.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:12.0],
        [_fileIcon.centerYAnchor constraintEqualToAnchor:card.centerYAnchor],
        [_fileIcon.widthAnchor constraintEqualToConstant:36.0],
        [_fileIcon.heightAnchor constraintEqualToConstant:36.0],
        [_badge.trailingAnchor constraintEqualToAnchor:_fileIcon.trailingAnchor constant:5.0],
        [_badge.bottomAnchor constraintEqualToAnchor:_fileIcon.bottomAnchor constant:2.0],
        [_badge.heightAnchor constraintEqualToConstant:14.0],
        [_badge.widthAnchor constraintGreaterThanOrEqualToConstant:26.0],
        [_filename.leadingAnchor constraintEqualToAnchor:_fileIcon.trailingAnchor constant:13.0],
        [_filename.trailingAnchor constraintLessThanOrEqualToAnchor:_pin.leadingAnchor constant:-8.0],
        [_filename.topAnchor constraintEqualToAnchor:card.topAnchor constant:9.0],
        [_path.leadingAnchor constraintEqualToAnchor:_filename.leadingAnchor],
        [_path.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-14.0],
        [_path.topAnchor constraintEqualToAnchor:_filename.bottomAnchor constant:3.0],
        [_pin.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-13.0],
        [_pin.topAnchor constraintEqualToAnchor:card.topAnchor constant:10.0],
    ]];
    return self;
}
- (void)applyItem:(const such::ui::ResultItem&)item {
    _filename.text = ns(item.filename);
    _path.text = ns(item.path);
    _badge.text = [ns(item.extension) uppercaseString];
    _pin.hidden = !item.pinned;

    NSURL* url = [NSURL fileURLWithPath:ns(item.path)];
    UIDocumentInteractionController* dc = [UIDocumentInteractionController interactionControllerWithURL:url];
    UIImage* icon = dc.icons.firstObject;
    if (!icon) icon = [UIImage systemImageNamed:@"doc"];
    _fileIcon.image = icon;
}
@end

@interface SuchViewController : UIViewController <UITableViewDataSource, UITableViewDelegate, UITextFieldDelegate>
@end

@implementation SuchViewController {
    UISearchTextField* _search;
    UITableView* _table;
    std::vector<such::ui::ResultItem> _results;
    BOOL _demo;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = paperColor();
    _demo = [NSProcessInfo.processInfo.arguments containsObject:@"--demo"];
    if (const auto preferred = such::ui::load_font_preference(); preferred && !preferred->empty()) {
        NSString* name = [NSString stringWithUTF8String:preferred->c_str()];
        if (name != nil && [UIFont fontWithName:name size:15.0] != nil) gSuchFontFamily = name;
    }

    _search = [[UISearchTextField alloc] initWithFrame:CGRectZero];
    _search.translatesAutoresizingMaskIntoConstraints = NO;
    _search.placeholder = @"2026 Heritage Inc.";
    _search.attributedPlaceholder = [[NSAttributedString alloc] initWithString:@"2026 Heritage Inc." attributes:@{NSForegroundColorAttributeName:[UIColor colorWithWhite:0.70 alpha:1.0]}];
    _search.autocorrectionType = UITextAutocorrectionTypeNo;
    _search.delegate = self;
    _search.font = suchFont(15.0, UIFontWeightRegular);
    _search.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _search.backgroundColor = brightColor();
    _search.textColor = darkGreen();
    _search.tintColor = darkGreen();
    _search.layer.cornerRadius = 7.0;
    _search.layer.borderWidth = 1.0;
    _search.layer.borderColor = darkGreen().CGColor;
    [_search addTarget:self action:@selector(searchChanged:) forControlEvents:UIControlEventEditingChanged];
    [self.view addSubview:_search];

    _table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    _table.backgroundColor = UIColor.clearColor;
    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    _table.rowHeight = 69.0;
    _table.scrollEnabled = YES;
    _table.dataSource = self;
    _table.delegate = self;
    [_table registerClass:[SuchCell class] forCellReuseIdentifier:@"SuchCell"];
    [self.view addSubview:_table];

    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_search.topAnchor constraintEqualToAnchor:safe.topAnchor constant:14.0],
        [_search.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:18.0],
        [_search.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-18.0],
        [_search.heightAnchor constraintEqualToConstant:50.0],
        [_table.topAnchor constraintEqualToAnchor:_search.bottomAnchor constant:10.0],
        [_table.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:18.0],
        [_table.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-18.0],
        [_table.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-8.0]
    ]];
    [self rebuildResults];
    dispatch_async(dispatch_get_main_queue(), ^{ [self->_search becomeFirstResponder]; });
}
- (BOOL)applyFontFamilyUtf8:(const std::string&)family {
    if (family.empty()) {
        gSuchFontFamily = nil;
        std::string error;
        (void)such::ui::clear_font_preference(&error);
    } else {
        NSString* name = [NSString stringWithUTF8String:family.c_str()];
        if (name == nil || [UIFont fontWithName:name size:15.0] == nil) return NO;
        gSuchFontFamily = name;
        std::string error;
        (void)such::ui::save_font_preference(family, &error);
    }
    _search.font = suchFont(15.0, UIFontWeightRegular);
    [self rebuildResults];
    return YES;
}
- (void)showFontPicker {
    UIAlertController* sheet = [UIAlertController alertControllerWithTitle:@"Font" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    const auto& options = such::ui::builtin_font_options();
    __weak SuchViewController* weakSelf = self;
    for (const auto& option : options) {
        NSString* title = [NSString stringWithUTF8String:option.label.c_str()];
        const std::string family = option.family;
        [sheet addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction* action) {
            (void)[weakSelf applyFontFamilyUtf8:family];
        }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"System Default" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction* action) {
        (void)[weakSelf applyFontFamilyUtf8:std::string{}];
    }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController* popover = sheet.popoverPresentationController;
    if (popover != nil) { popover.sourceView = _search; popover.sourceRect = _search.bounds; }
    [self presentViewController:sheet animated:YES completion:nil];
}
- (BOOL)textFieldShouldReturn:(UITextField*)textField {
    (void)textField;
    const char* utf8 = _search.text.UTF8String;
    const std::string query = utf8 != nullptr ? utf8 : "";
    const auto cmd = such::ui::parse_font_command(query, such::ui::PlatformDialect::UnixLike);
    if (!cmd.matched) return YES;
    if (cmd.show_picker) [self showFontPicker];
    else if (cmd.requested_family.has_value()) (void)[self applyFontFamilyUtf8:*cmd.requested_family];
    _search.text = @"";
    [self rebuildResults];
    return NO;
}
- (void)searchChanged:(UISearchTextField*)sender { (void)sender; [self rebuildResults]; }
- (void)rebuildResults {
    _results.clear();
    if (_demo) {
        const char* utf8 = _search.text.UTF8String;
        std::string query = utf8 != nullptr ? utf8 : "";
        if (such::ui::parse_font_command(query, such::ui::PlatformDialect::UnixLike).matched) { [_table reloadData]; return; }
        _results = such::ui::make_demo_results(query, such::ui::PlatformDialect::UnixLike, static_cast<std::int64_t>(std::time(nullptr)));
    }
    [_table reloadData];
}
- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView; (void)section;
    return (NSInteger)_results.size();
}
- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath {
    SuchCell* cell = [tableView dequeueReusableCellWithIdentifier:@"SuchCell" forIndexPath:indexPath];
    [cell applyItem:_results[(std::size_t)indexPath.row]];
    return cell;
}
- (UISwipeActionsConfiguration*)tableView:(UITableView*)tableView trailingSwipeActionsConfigurationForRowAtIndexPath:(NSIndexPath*)indexPath {
    (void)tableView;
    __weak SuchViewController* weakSelf = self;
    const BOOL validRow = indexPath.row < (NSInteger)_results.size();
    const BOOL indexed = validRow ? _results[(std::size_t)indexPath.row].indexed : NO;
    const BOOL pinned = validRow ? _results[(std::size_t)indexPath.row].pinned : NO;
    UIContextualAction* location = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal title:@"Location" handler:^(UIContextualAction* a, UIView* v, void (^done)(BOOL)){
        (void)a; (void)v;
        // Files/Document Provider reveal must be supplied by the production runtime
        // together with a security-scoped URL. Synthetic demo paths are not claimed
        // as successful filesystem actions.
        done(NO);
    }];
    UIContextualAction* index = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal title:(indexed ? @"Unindex" : @"Index") handler:^(UIContextualAction* a, UIView* v, void (^done)(BOOL)){
        (void)a; (void)v;
        SuchViewController* strongSelf = weakSelf;
        if (strongSelf && strongSelf->_demo && indexPath.row < (NSInteger)strongSelf->_results.size()) {
            auto& item = strongSelf->_results[(std::size_t)indexPath.row];
            item.indexed = !item.indexed;
            [strongSelf->_table reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
            done(YES);
            return;
        }
        done(NO);
    }];
    UIContextualAction* pin = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal title:(pinned ? @"Unpin" : @"Pin") handler:^(UIContextualAction* a, UIView* v, void (^done)(BOOL)){
        (void)a; (void)v;
        SuchViewController* strongSelf = weakSelf;
        if (strongSelf && strongSelf->_demo && indexPath.row < (NSInteger)strongSelf->_results.size()) {
            auto& item = strongSelf->_results[(std::size_t)indexPath.row];
            item.pinned = !item.pinned;
            [strongSelf->_table reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
            done(YES);
            return;
        }
        done(NO);
    }];
    location.backgroundColor = darkGreen();
    index.backgroundColor = [UIColor colorWithRed:34.0/255.0 green:76.0/255.0 blue:58.0/255.0 alpha:1.0];
    pin.backgroundColor = [UIColor colorWithRed:47.0/255.0 green:91.0/255.0 blue:70.0/255.0 alpha:1.0];
    UISwipeActionsConfiguration* cfg = [UISwipeActionsConfiguration configurationWithActions:@[pin,index,location]];
    cfg.performsFirstActionWithFullSwipe = NO;
    return cfg;
}
- (UISwipeActionsConfiguration*)tableView:(UITableView*)tableView leadingSwipeActionsConfigurationForRowAtIndexPath:(NSIndexPath*)indexPath {
    (void)tableView; (void)indexPath;
#if defined(SUCH_EXPERIMENTAL_LEADING_SWIPE)
    __weak SuchViewController* weakSelf = self;
    UIContextualAction* pin = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal title:@"Pin" handler:^(UIContextualAction* a, UIView* v, void (^done)(BOOL)){
        (void)a; (void)v;
        SuchViewController* strongSelf = weakSelf;
        if (strongSelf && strongSelf->_demo && indexPath.row < (NSInteger)strongSelf->_results.size()) {
            auto& item = strongSelf->_results[(std::size_t)indexPath.row];
            item.pinned = !item.pinned;
            [strongSelf->_table reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
            done(YES);
            return;
        }
        done(NO);
    }];
    UIContextualAction* properties = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal title:@"Properties" handler:^(UIContextualAction* a, UIView* v, void (^done)(BOOL)){
        (void)a; (void)v;
        // Requires a production Document Provider/runtime bridge. Do not report
        // success for a placeholder action.
        done(NO);
    }];
    UIContextualAction* appearance = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal title:@"Appearance" handler:^(UIContextualAction* a, UIView* v, void (^done)(BOOL)){
        (void)a; (void)v;
        // Appearance persistence is runtime-owned; no local fake success.
        done(NO);
    }];
    pin.backgroundColor = darkGreen();
    properties.backgroundColor = [UIColor colorWithRed:34.0/255.0 green:76.0/255.0 blue:58.0/255.0 alpha:1.0];
    appearance.backgroundColor = [UIColor colorWithRed:47.0/255.0 green:91.0/255.0 blue:70.0/255.0 alpha:1.0];
    UISwipeActionsConfiguration* cfg = [UISwipeActionsConfiguration configurationWithActions:@[pin,properties,appearance]];
    cfg.performsFirstActionWithFullSwipe = YES;
    return cfg;
#else
    return nil;
#endif
}
@end

@interface SuchAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation SuchAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application; (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[SuchViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        gLease.emplace(such::ui::FrontendLease::try_acquire(such::ui::FrontendMode::Gui));
        if (!gLease->acquired()) return 23;
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([SuchAppDelegate class]));
    }
}

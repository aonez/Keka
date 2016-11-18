//
//  AppDelegate.h
//  Keka Translator
//
//  Created by aONe on 3/3/13.
//  Copyright (c) 2013 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate> {
    IBOutlet NSTextField * langString;
}

@property (assign) IBOutlet NSWindow *window;

- (IBAction)test:(id)sender;
- (IBAction)showFiles:(id)sender;
- (IBAction)showKeka:(id)sender;

- (void)downloadKeka;
- (void)deletePreviousTranslations:(NSString *)kekaResourcesPath;
- (void)copyNewTranslation:(NSString *)kekaResourcesPath;

@end

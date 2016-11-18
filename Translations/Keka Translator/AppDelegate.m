//
//  AppDelegate.m
//  Keka Translator
//
//  Created by aONe on 3/3/13.
//  Copyright (c) 2013 __MyCompanyName__. All rights reserved.
//

#import "AppDelegate.h"
#import "LANGUAGE.h"

@implementation AppDelegate

@synthesize window = _window;

- (void)dealloc
{
    [super dealloc];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    // Insert code here to initialize your application
    NSString * kekaPath = [[NSBundle mainBundle] pathForResource:@"Keka" ofType:@"app"];

    if (!kekaPath.length)
        [self downloadKeka];
    
    [_window makeKeyAndOrderFront:self];

    [_window setLevel:NSFloatingWindowLevel];
}

-(BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication
{
    return YES;
}

- (IBAction)showFiles:(id)sender
{
    NSString * translationPath = [[NSBundle mainBundle] pathForResource:@"Translation" ofType:nil];
    NSLog(@"%@",translationPath);
    [[NSWorkspace sharedWorkspace] selectFile:translationPath inFileViewerRootedAtPath:translationPath];
}

- (void)downloadKeka {
    [NSAlert alertWithMessageText:@"Keka needs to be downloaded first" defaultButton:@"Ok" alternateButton:@"Cancel" otherButton:nil informativeTextWithFormat:@"To be able to test your translation, Keka.app needs to be downloaded first within Keka Translator"];
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert addButtonWithTitle:NSLocalizedString(@"Ok",nil)];
    [alert addButtonWithTitle:NSLocalizedString(@"Maybe I'll try later",nil)];
    [alert setMessageText:NSLocalizedString(@"Keka needs to be downloaded first",nil)];
    [alert setInformativeText:NSLocalizedString(@"To be able to test your translation, Keka.app needs to be downloaded first within Keka Translator.\n\nThe process will be done in the background, then Keka Translator.app will reopen automagically ;)",nil)];
    [alert setAlertStyle:NSInformationalAlertStyle];
    bool doit = true;
    if ([alert runModal]==NSAlertFirstButtonReturn) {
        doit = true;
    }
    if (doit)
    {
        dispatch_async(dispatch_get_global_queue( DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSString *getkeka = [[NSBundle mainBundle] pathForResource:@"getkeka" ofType:@"sh"];
            getkeka = [NSString stringWithFormat:@"\"%@\"", getkeka];
            NSLog(@"%@",getkeka);
            system([getkeka UTF8String]);
        });
    }
}



- (IBAction)showKeka:(id)sender
{
    NSString * kekaPath = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"Keka.app"];
    [[NSWorkspace sharedWorkspace] selectFile:kekaPath inFileViewerRootedAtPath:[kekaPath stringByDeletingLastPathComponent]];
}

- (IBAction)test:(id)sender
{
    NSString * kekaPath = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"Keka.app"];
    NSString * kekaResourcesPath = [kekaPath stringByAppendingString:@"/Contents/Resources"];
    
    [self deletePreviousTranslations:kekaResourcesPath];
    [self copyNewTranslation:kekaResourcesPath];
    
    NSLog(@"Launching Keka.app");
    [[NSWorkspace sharedWorkspace] launchApplication:kekaPath];
    
    if (BUILD_AND_EXIT) {
        NSLog(@"All done here :)");
        [NSApp terminate:nil];
    }
}

- (void)deletePreviousTranslations:(NSString *)kekaResourcesPath
{
    NSLog(@"Deleting old translations...");
    NSError * someError = nil;
    NSArray * contentsOfKeka = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:kekaResourcesPath error:&someError];
    
    if (someError!=nil)
        NSLog(@"%@",[someError localizedDescription]);
    
    if (contentsOfKeka!=nil) {
        NSEnumerator * contentsEnum = [contentsOfKeka objectEnumerator];
        id currentObject;
        while (currentObject = [contentsEnum nextObject]) {
            if ([currentObject rangeOfString:@".lproj"].length>0) {
                NSLog(@"Deleting ""%@""...",currentObject);
                [[NSFileManager defaultManager] removeItemAtPath:[kekaResourcesPath stringByAppendingPathComponent:currentObject] error:&someError];
                if (someError!=nil)
                    NSLog(@"%@",[someError localizedDescription]);
            }
        }
    }
    else {
        NSLog(@"No contents found... Is Keka.app in the project root path?!");
    }
    NSLog(@"Done deleting.");
}

- (void)copyNewTranslation:(NSString *)kekaResourcesPath
{
    NSLog(@"Copying new translation files...");
    NSError * someError = nil;
    NSArray * filesToCopy = [NSArray arrayWithObjects:@"advanced.nib",
                             @"compression.nib",
                             @"Credits.html",
                             @"extraction.nib",
                             @"Localizable.strings",
                             @"main.nib",
                             @"preferences.nib",
                             nil];
    NSString * newLanguageString = DEFAULT_LANG;
    NSLog(@"Creating new language path ""%@""...",newLanguageString);
    NSString * newLanguagePath = [kekaResourcesPath stringByAppendingPathComponent:newLanguageString];
    BOOL dirCreated = [[NSFileManager defaultManager] createDirectoryAtPath:newLanguagePath withIntermediateDirectories:NO attributes:nil error:&someError];
    
    if (someError!=nil)
        NSLog(@"%@",[someError localizedDescription]);
    
    if (dirCreated) {
        NSEnumerator * contentsEnum = [filesToCopy objectEnumerator];
        id currentObject;
        
        while (currentObject = [contentsEnum nextObject]) {
            NSLog(@"Copying ""%@""...",currentObject);
            NSString * filePath = [[[[NSBundle mainBundle] bundlePath] stringByAppendingString:@"/Contents/Resources"] stringByAppendingPathComponent:currentObject];
            if (filePath!=nil) {
                [[NSFileManager defaultManager] copyItemAtPath:filePath toPath:[newLanguagePath stringByAppendingPathComponent:currentObject] error:&someError];
                if (someError!=nil) {
                    NSLog(@"%@",[someError localizedDescription]);
                    NSLog(@"Path of ""%@"":\n%@",currentObject,filePath);
                }
            }
            else
                NSLog(@"Unable to find ""%@"" path...",currentObject);
        }
    }
    else {
        NSLog(@"Unable to create ""%@""...",newLanguagePath);
    }
    NSLog(@"Done copying.");
}

@end

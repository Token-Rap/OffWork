#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#include <string.h>

// Rust owns the returned UTF-8 buffer and releases it with free().
char *offwork_vision(const char *path) {
 @autoreleasepool {
  NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
  VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
  request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
  NSArray *supported = [request supportedRecognitionLanguagesAndReturnError:nil];
  if ([supported containsObject:@"zh-Hans"]) request.recognitionLanguages = @[@"zh-Hans",@"en-US"];
  VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithURL:url options:@{}];
  if (![handler performRequests:@[request] error:nil]) return NULL;
  NSMutableArray *lines = [NSMutableArray array];
  for (VNRecognizedTextObservation *row in request.results) {
   NSString *line = [[row topCandidates:1] firstObject].string;
   if (line) [lines addObject:line];
  }
  return strdup([lines componentsJoinedByString:@"\n"].UTF8String);
 }
}

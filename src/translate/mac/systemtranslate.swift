// macOS 系统翻译（Translation 框架，离线）。框架只有 Swift 接口，这里导出几个 C 函数给 C++ 调用。
// 翻译要求 macOS 26（不依赖 SwiftUI 的 TranslationSession(installedSource:)）；语言包没下载时，
// 由 snappin_system_prepare 打开一个小窗口，借 SwiftUI 的 translationTask 弹出系统的下载确认

import AppKit
import Foundation
import NaturalLanguage
import SwiftUI
import Translation

// 回调可能在任意线程：ctx 原样带回，json 是译文数组（失败时为空），error 为空表示成功，
// code：0 成功，1 一般错误，2 语言包未下载（此时 error 里是 "源语言 目标语言"）
public typealias SnapPinTranslateCallback = @convention(c) (
    UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?, Int32
) -> Void

private func reply(_ cb: SnapPinTranslateCallback, _ ctx: UnsafeMutableRawPointer?, json: String?, error: String?, code: Int32) {
    if let json = json {
        json.withCString { j in cb(ctx, j, nil, code) }
    } else {
        (error ?? "").withCString { e in cb(ctx, nil, e, code) }
    }
}

// 识别源语言：中文统一按简体处理，没把握时返回 nil
private func detectLanguage(_ text: String) -> Locale.Language? {
    let recognizer = NLLanguageRecognizer()
    recognizer.processString(text)
    guard let lang = recognizer.dominantLanguage else { return nil }
    return Locale.Language(identifier: lang.rawValue)
}

// 系统翻译会把单个换行变成空行；原文里没有空行时，把译文里连续的换行合成一个，左右对照时行数对得上
private func keepLineBreaks(_ source: String, _ translated: String) -> String {
    if source.contains("\n\n") { return translated }
    return translated.replacingOccurrences(of: "\n{2,}", with: "\n", options: .regularExpression)
}

private func sameLanguage(_ a: Locale.Language, _ b: Locale.Language) -> Bool {
    return a.languageCode == b.languageCode
        && (a.languageCode?.identifier != "zh" || a.script == nil || b.script == nil || a.script == b.script)
}

@_cdecl("snappin_system_translate_supported")
public func snappinSystemTranslateSupported() -> Int32 {
    if #available(macOS 26.0, *) { return 1 }
    return 0
}

// texts：JSON 字符串数组；target：BCP 47 语言代码（zh-Hans、en、ja…）
@_cdecl("snappin_system_translate")
public func snappinSystemTranslate(_ textsJson: UnsafePointer<CChar>, _ target: UnsafePointer<CChar>,
                                   _ ctx: UnsafeMutableRawPointer?, _ cb: SnapPinTranslateCallback) {
    let data = Data(String(cString: textsJson).utf8)
    let targetId = String(cString: target)
    guard #available(macOS 26.0, *) else {
        reply(cb, ctx, json: nil, error: "系统翻译需要 macOS 26 或更新版本", code: 1)
        return
    }
    guard let texts = try? JSONDecoder().decode([String].self, from: data) else {
        reply(cb, ctx, json: nil, error: "参数错误", code: 1)
        return
    }
    Task.detached {
        let targetLang = Locale.Language(identifier: targetId)
        guard let source = detectLanguage(texts.joined(separator: "\n")) else {
            reply(cb, ctx, json: nil, error: "无法判断原文的语言", code: 1)
            return
        }
        // 原文已经是目标语言，不用翻译
        if sameLanguage(source, targetLang) {
            let out = try! JSONEncoder().encode(texts)
            reply(cb, ctx, json: String(decoding: out, as: UTF8.self), error: nil, code: 0)
            return
        }
        let status = await LanguageAvailability().status(from: source, to: targetLang)
        switch status {
        case .unsupported:
            reply(cb, ctx, json: nil, error: "系统翻译不支持从 \(source.minimalIdentifier) 翻译到 \(targetId)", code: 1)
            return
        case .supported:
            reply(cb, ctx, json: nil, error: "\(source.minimalIdentifier) \(targetId)", code: 2)
            return
        default:
            break
        }
        do {
            let session = TranslationSession(installedSource: source, target: targetLang)
            // 批量翻译，按 clientIdentifier 对回原来的顺序
            let requests = texts.enumerated().map {
                TranslationSession.Request(sourceText: $0.element, clientIdentifier: String($0.offset))
            }
            var out = texts
            for response in try await session.translations(from: requests) {
                if let id = response.clientIdentifier, let i = Int(id), i < out.count {
                    out[i] = keepLineBreaks(texts[i], response.targetText)
                }
            }
            let json = try JSONEncoder().encode(out)
            reply(cb, ctx, json: String(decoding: json, as: UTF8.self), error: nil, code: 0)
        } catch {
            reply(cb, ctx, json: nil, error: error.localizedDescription, code: 1)
        }
    }
}

@available(macOS 15.0, *)
private struct PrepareView: View {
    let source: Locale.Language
    let target: Locale.Language
    let done: (String?) -> Void
    @State private var config: TranslationSession.Configuration?

    var body: some View {
        VStack(spacing: 10) {
            ProgressView()
            Text("正在准备翻译语言包…")
        }
        .frame(width: 300, height: 110)
        .onAppear { config = TranslationSession.Configuration(source: source, target: target) }
        .translationTask(config) { session in
            do {
                try await session.prepareTranslation()
                done(nil)
            } catch {
                done(error.localizedDescription)
            }
        }
    }
}

private var prepareWindows: [NSWindow] = []

// 必须在主线程调用：弹出系统的语言包下载确认，下载完成（或用户取消）后回调，json 为空
@_cdecl("snappin_system_prepare")
public func snappinSystemPrepare(_ source: UnsafePointer<CChar>, _ target: UnsafePointer<CChar>,
                                 _ ctx: UnsafeMutableRawPointer?, _ cb: SnapPinTranslateCallback) {
    guard #available(macOS 15.0, *) else {
        reply(cb, ctx, json: nil, error: "系统翻译需要 macOS 26 或更新版本", code: 1)
        return
    }
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 300, height: 110),
                          styleMask: [.titled], backing: .buffered, defer: false)
    window.title = "SnapPin"
    window.isReleasedWhenClosed = false
    window.level = .floating
    let view = PrepareView(source: Locale.Language(identifier: String(cString: source)),
                           target: Locale.Language(identifier: String(cString: target))) { error in
        DispatchQueue.main.async {
            window.close()
            prepareWindows.removeAll { $0 === window }
            if let error = error {
                reply(cb, ctx, json: nil, error: error, code: 1)
            } else {
                reply(cb, ctx, json: "[]", error: nil, code: 0)
            }
        }
    }
    window.contentViewController = NSHostingController(rootView: view)
    prepareWindows.append(window)
    window.center()
    NSApp.activate(ignoringOtherApps: true)
    window.makeKeyAndOrderFront(nil)
}

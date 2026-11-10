import QtQuick
import QtTest

import StatusQ.Components
import StatusQ.Core.Theme

Item {
    id: root

    width: 400
    height: 400

    readonly property url videoUrl: "https://example.com/collectible.mp4"
    readonly property url stillUrl: Assets.png("chat/chat@2x")

    Component {
        id: mediaComponent

        StatusRoundedMedia {
            width: 100
            height: 100
            interactive: true
        }
    }

    SignalSpy {
        id: videoClickedSpy
        signalName: "videoClicked"
    }

    SignalSpy {
        id: imageClickedSpy
        signalName: "imageClicked"
    }

    TestCase {
        name: "StatusRoundedMedia"
        when: windowShown

        function mediaItem(item) {
            for (let i = 0; i < item.children.length; ++i) {
                const child = item.children[i]
                if (child instanceof Loader && child.item && child.item.source !== undefined)
                    return child.item
                const found = mediaItem(child)
                if (found)
                    return found
            }
            return null
        }

        function createMedia(properties) {
            const media = createTemporaryObject(mediaComponent, root, properties)
            videoClickedSpy.clear()
            imageClickedSpy.clear()
            videoClickedSpy.target = media
            imageClickedSpy.target = media
            tryVerify(() => !media.isLoading)
            return media
        }

        function test_videoWithoutPlaybackShowsStillAndEmitsVideoClicked() {
            const media = createMedia({ videoPlaybackEnabled: false, mediaType: "video/mp4",
                                        mediaUrl: root.videoUrl, fallbackImageUrl: root.stillUrl })

            verify(!media.isError)
            compare(media.componentMediaType, StatusRoundedMedia.MediaType.Video)
            compare(mediaItem(media).source, root.stillUrl)

            mouseClick(media)
            compare(videoClickedSpy.count, 1)
            compare(videoClickedSpy.signalArguments[0][0], root.videoUrl)
            compare(imageClickedSpy.count, 0)
        }

        function test_imageWithoutPlaybackEmitsImageClicked() {
            const media = createMedia({ videoPlaybackEnabled: false, mediaType: "image/png",
                                        mediaUrl: root.stillUrl })

            compare(mediaItem(media).source, root.stillUrl)

            mouseClick(media)
            compare(imageClickedSpy.count, 1)
            compare(videoClickedSpy.count, 0)
        }
    }
}
